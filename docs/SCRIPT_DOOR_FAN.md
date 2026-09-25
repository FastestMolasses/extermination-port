# Script host, door and fan/husk lanes (`script-door-fan`)

Lane "script-door-fan" (build lane `b7-script-door-fan`, session s87,
2026-09-23). It covers three census lanes of `FIRST_LEVEL_CENSUS.md`:
**L19-script-host**, **L18-door-original** and **L24-fan-husk**. It
translates the rows marked missing and stand-in and verifies the rows marked
unverified. For the verified-unbound rows it adds binding notes only.

- Code: `src/game/em_script_door_fan.{h,c}` (boot-ELF leaves) and
  `src/game/em_script_door_fan_husk.{h,c}` (the AREA11 overlay owners
  0x825940, 0x827490 and 0x823CE0).
- Oracle: `tools/test_script_door_fan_reference.py`. It takes about 3 s by
  default and about 60 s with `EM_TEST_FULL=1`.
- Sanitizer fixture: `tests/script_door_fan_test.c` (ASan/UBSan, 55 checks, no
  game data).

**Status: built and oracle-verified, not wired.** No live file includes
either module. Section 5 gives the binding, and section 7 lists the changes to
existing files that only the lead or coordinator may make.

Addresses are original runtime addresses. Overlay functions are cited at
runtime; the splat listing names each one 0x40 lower (the MWo3 header
address). The overlay file offset is the runtime address minus 0x823500.

## 1. Rows: census status before → after

"v-u" means verified-unbound. "Oracle" names the test that executes the
original instructions of the row with no hook on them.

### L19-script-host (21 rows)

| Function | Before | After | Translation | Oracle |
|---|---|---|---|---|
| 001B6BF0 op18 skip landing | unverified | **v-u** | `em_area_script` op18 | this test, part 1 |
| 001B6E40 op16 take frame | unverified | **v-u** | `em_area_script` op16 | part 1 |
| 001B6F80 player yaw + 00182F90 | unverified | **v-u** | `em_area_script` op01 sub9 (inline) | part 1 |
| 001B6FA0 op15 turn and talk | unverified | **v-u** | `em_area_script` op15 | part 1 |
| 001B7840 op10 fades | unverified | **v-u** | `em_area_script` op10 | part 1 |
| 001B81D0 face attach | unverified | **v-u** | `em_area_script` `face_attach` | part 1 |
| 001BA080 op06 flags/counters | unverified | **v-u** | `em_area_script` op06 | part 1 |
| 001BAC00 op14 spawn list | unverified (em_opening_runtime case 20 is a no-op stand-in) | **v-u** | `em_sdf_001BAC00` (new) | part 2 + opening capture |
| 001BA510 activity clear | missing | **v-u** | `em_sdf_001BA510` (new); also inline in `em_interaction_frame` (see 1.4) | part 2 |
| 001BAD40 spawned-actor event handler | missing | **v-u** | `em_sdf_001BAD40` (new) | part 2 + opening capture |
| 00182BF0, 001B0460, 001B12B0, 001B1380, 001B6250 | v-u | v-u | `em_script_host_workers` | test_script_host_workers_reference |
| 001B0B50 | v-u | v-u | `em_player_closure1019_001B0B50` | test_player_closure_10_12_19_reference |
| 001B15D0 | v-u | v-u | `em_player_misc_001B15D0` | test_player_misc_workers_reference |
| 001B7B30 op0D | v-u | v-u | `em_cinematic_playback` (sub0) + `em_area_script` (subs 1–8) | test_cinematic_playback / test_area_script |
| 001B8020 op0B | v-u | v-u | `em_area_script` op0B sub4 | test_area_script_reference |
| 001B8FC0 op00 | v-u | v-u | `em_area_script` op00 | test_area_script_reference |
| 001BA1A0 script start | v-u | v-u | `em_area_script_start`, `em_director_original` | test_area_script / test_director_original |

### L18-door-original (11 rows)

| Function | Before | After | Translation | Oracle |
|---|---|---|---|---|
| 001B1B30 visibility publish | unverified (legacy em_door.c) | **v-u** | `em_sdf_001B1B30` (new) | part 2 |
| 001BC240 door phase 4 | unverified (legacy em_door.c) | **v-u** | `em_sdf_001BC240` (new); `em_door_original` phase 4 inline | part 2 + route 09; test_door_original_reference runs it unhooked inside 001BC350 |
| 001BC290 door phase 5 | unverified (legacy em_door.c) | **v-u** | `em_sdf_001BC290` (new); `em_door_original` phase 5 inline | part 2 + route 09; test_door_original_reference as above |
| 001BBD60 door sound patch | stand-in (em_door.c sound patch) | **v-u** | `em_sdf_001BBD60` (new) | part 2 + route 09 capture |
| 001B0080 room-entry camera seat | v-u **(wrong: no translation existed, only the worker slot `w_001B0080` of em_script_host_workers, hooked in its test)** | **v-u** | `em_sdf_001B0080` (new) | part 2 |
| 001BC350, 001BBDA0, 001BC0E0, 001BC300 | v-u | v-u | `em_door_original` | test_door_original_reference |
| 001BBE40 | v-u | v-u | `em_door_transit_kickoff` | test_door_transit_reference |
| 001B94F0 op01 | v-u | v-u | `em_area_script` op01 | test_area_script_reference |

### L24-fan-husk (4 rows)

| Function | Before | After | Translation | Oracle |
|---|---|---|---|---|
| 0x827630 fan pair | v-u | v-u | `em_fan_original` | test_fan_original_reference |
| 0x825940 husk creature | stand-in (em_enemy.c legacy `em_enemy_update`, interim 001C5570 child spawn) | **v-u, partial**: lifecycles 0, 0x64, 2, 3 and "other" are translated; lifecycles 1 and 4 fault | `em_husk_creature_tick` (new) | part 3 |
| 0x827490 husk partner | stand-in (em_enemy.c legacy) | **v-u** | `em_husk_partner_tick` (new) | part 3 |
| 0x823CE0 manager r11 | missing (a dormant node with no code) | **v-u** | `em_husk_manager_tick` (new) | part 3 |

### 1.4 Census corrections found on the way

- **001B0080** is not verified-unbound. Before this lane nothing translated
  it. `em_script_host_001B0460` only calls it through a worker, and its test
  hooks it. It runs at S1 and at the beat-09 room move. It is now translated
  (`em_sdf_001B0080`).
- **001BA510** is inlined and executed. `em_interaction_frame` clears its
  `activity[12]` inline, and `test_interaction_frame_reference.py` executes the
  original range 0x1BA510..0x1BA550 inside 001B82D0 (see its pc assertion).
  So the row was never truly missing. The standalone translation is for other
  callers.
- **001BC240 / 001BC290** are verified inside `em_door_original`.
  `test_door_original_reference.py` runs 001BC350 unhooked, and 001BC350
  calls both (only their callees 001C64F0, 001BC150 and 001C67E0 are hooked).
  The census credited only the legacy em_door.c.
- **The seven em_area_script handler rows** were already executed as
  original instructions by `test_area_script_reference.py`. 001BA1F0
  dispatches to them through ftab_0024D880 and none is hooked. The census
  missed this. This lane adds the missing path coverage (section 3.1).

## 2. What the new translations do

### em_script_door_fan.c (boot ELF)

- **001BA510** clears D_008106D4..D_008106DF, the 12 "activity" bytes at
  +0x24 of the D_008106B0 request block, in ascending order.
- **001BAC00** is script op14 (ftab_0024D880[0x14]). Byte-matched C.
  - It walks the 0x2C-byte entries starting at record +0x14. The first entry is
    always processed (a do-while). The walk stops at an entry whose first short
    is -1.
  - **Tag 0x270E** (entry +4): it spawns with
    001C8140(D_0028A490[entry +6], entry +8, entry +0x28). On success it sets
    node +0x24 = owner +0x14 and node +0x2E = 0xF.
  - **Every other entry**: it spawns with 001AFA90(entry byte 0). On success
    it copies +3 and +0xD, then the position and rotation floats. It sets +0x10
    to entry +0x28, or to 001BB0E0 when entry +0xA == 3 or that word is 0. It
    also sets +0x20 = the entry address, +0x24 = owner +0x14, and +0x2E = the
    running index.
  - The index counts only non-0x270E entries, and it counts failed spawns too.
  - At the end, owner +0x2E = 0 and the result is 1.
- **001BAD40** is the spawned actor's event handler (NEARMISS; read from its
  listing). It reads the entry at node +0x20.
  - **Message 0x270D** (entry +4): it arms the message request (D_002821B0 = 2,
    D_002821B4 = 1, D_002821B8 = entry +8) and clears +0xC and +9.
  - **Message 0x270C**: it publishes +0x18 to D_008106C0.
  - **Otherwise** it switches on the command at entry +0xA through the jump
    table 0x26E170. That table was read, not copied. Its mapping is: commands
    0/4/6 bind D_0028A490[msg]; 1/8 bind 001C6120(D_0028A59C, msg); 2 binds
    001C6120(D_0028A56C, msg); 3 starts the camera track; 5 runs 001C5C90(obj)
    and returns (lifecycle ≥ 2); 7 and every value ≥ 9 (an unsigned compare) go
    to the common tail.
  - **Command 3** sets D_00810250 = 001C6120(D_0028A490[entry +6], entry +8),
    D_00810254 = 0, D_00810258 = the first float at the track, D_008101E4 = 3
    and D_0081024E = the low short of entry +0x28. It then calls
    0022EC30(0x8101E0), clears +0xC and +9, and returns 0.
  - **The tail** sets +0x40 = D_0028A490[entry +6] and +0xC = 001C6150(+0x44).
    If the signed budget D_00275BCC is below the count, it sets +4 = 3 and
    returns 1. Otherwise it stores one 001AF780 handle per bone at +0x110, and
    sets +9 = the count. A second dispatch then calls 001CA6F0 with mode 1
    (command 8), 2 or 0 after 001D8BF0(obj, 1) (command 6) or after
    001BA8E0(obj, msg) (command 0) depending on whether +9 ≥ 3, and 0 otherwise.
    Then come 001CB5B0(+9) and 001C63E0(obj, entry +8). For command 4 only,
    it calls anim_clip_init(obj, entry +8, 0, (float)(001C61D0(+0x40, entry +8)
    − 1)).
  - **Differences from the decomp's NEARMISS comment.** 001C5C90 receives the
    actor in a0. 001C6120 gets msg as its second argument in commands 1, 2
    and 8.
- **001B1B30** sets +1 = the byte of 001B1630(x, y, z). When that byte is
  non-zero it calls 001B1B70(actor). It returns +1.
- **001BC240** sets block +0xE (actor +0x1FE) = anim_advance_time(actor, 1.0)
  and then calls 001BC150(actor).
- **001BC290** sets block +0xE the same way. If D_008106B8 == 0, it calls
  anim_clip_init(actor, 0, 0, 0), sets +0xB = 0 and returns 1. Otherwise it
  returns 0.
- **001BBD60** sets record word +0x18 = the halfword of the ELF table
  D_0024DB80 at row ((link +0x56 & 0xFF00) >> 8) × 4 plus column side
  (+0x2E) × 2, zero-extended.
- **001B0080** (camera object D_008101E0, float a1):
  - In area 1, room 4 (D_00810700 == 1, D_00810702 == 4) it stores the fixed
    eye (−4.3, 21.7, −572.5, 1) and target (39.7, 16.1, −557.5, 1).
  - Otherwise:
    - target = the player +0xA0 quad, with y + 17;
    - angles = the 0x70003B50 quad, with the yaw wrapped by 001B1470;
    - the 0x70003400 matrix is set to the identity (001029C0) and rotated by
      the angles (00102C58);
    - 0x70003600 = (0, 0, +0x0C, 1);
    - eye = 001026A0(matrix, that vector) + target, where y also adds a1 (a1
      is added to the target y first).
  - Either way, D_008105E0 = target and D_008105D0 = eye. The adds go through
    the EE add model.

### em_script_door_fan_husk.c (AREA11 overlay, no decomp C)

- **Husk creature 0x825940.** It dispatches on +4.
  - **Lifecycle 0** runs 001B0FD0 (non-zero result: return), then sets +4 = 4
    when D_00810788 == 0xFF (001BA1C0, inline) and 0x64 otherwise, and +0 = 1.
    It sets +0x28 = 300 + ((300 × (rand >> 16)) >> 15), using 00122BB8 and the
    listing's arithmetic shifts. It stores the +0x1F4..+0x218 words, all in
    listing order.
  - Still in lifecycle 0, it computes bone 3 +0x78 = −1.1344 + (−0.8290) ×
    sin(+0x1FC). The constants are the bit patterns in the code. Bone 3 is the
    slot word at D_00275B40 + 0xC.
  - It then calls 001C6380, and 001A2370(actor, bone3 + 0x90), and sets +0x220
    = 0. It spawns the class-0xC child through 001AFA90. The child gets +0x9A,
    +3, +0x2E, +0xD = 0x7A, +0xE = 0xFFFF, +0x54, +0x56, +0xA0..+0xAC = (0, 0,
    0, 0.25), the quad copies of +0xB0/+0xC0, and +0x10 = 001C5680. The
    creature then sets +0x220 = child and +0x224 = 0.
  - **Lifecycle 0x64 (dormant)** sets +4 = 4 when the flag is 0xFF. Then it
    calls 001C6380, 001B17A0 and the +0x4C draw.
  - **Lifecycle 2** checks the flag first; with the flag not 0xFF it runs only
    the three calls above.
    - With the flag, it moves +0x1FC toward −π/2 by +0x1F8, clamped (EE
      add/sub and compare), and sets bone 3 +0x78 = −0.8290 + 0.3054 ×
      sin(+0x1FC).
    - It then copies the matrix of the creature's bone 3 onto the child's
      (00102958).
    - At exactly −π/2 it counts +0x21C down, puts (+0x21C / 90) into
      0x70003A20, and drives the child's +0xA0 vector: x or y, chosen by +0x224.
  - **Lifecycle 3 and every value not listed** call 001AFC10 (free).
  - **Lifecycles 1 and 4 are not translated.** The native faults at 0x826190 or
    0x825B74 (EM_HUSK_FAULT_UNTRANSLATED). See section 6.
- **Husk partner 0x827490.**
  - **Lifecycle 0:** 001B0FD0; then +0x34 = 1 and +0 = 1. If 001B11E0(+0x9A)
    reports the taken bit, the linked record at +0x18 (the creature) gets
    +4 = 2, and the partner sets its own +4 = 3.
  - **Lifecycle 1:** when shot (+0x36 ≠ 0) it sets +0 = 2, linked +4 = 2 and
    linked +0x21C = 0x5A, then calls 001EFE00(0x80000045, actor). If that FX
    call returns 0, it sets +4 = 3. Otherwise it plays sound 0x426 (range 300),
    sets +0x28 = 0 and +4 = 2. When not shot it calls 001C6380, 001B17A0 and
    the draw.
  - **Lifecycle 2:** +0x28 counts up to 10, and sound 0x427 plays when it
    reaches 10. Then 001B1190(+0x9A) (set the taken bit), 001B17A0 and the
    draw.
  - **3 and anything else:** free.
- **Manager 0x823CE0.**
  - **Lifecycle 0:** with D_00810788 == 0xFF it sets +4 = 3. Otherwise it
    sets +4 = 1 and +0 = 1.
  - **Lifecycle 1, phase 0:** when D_00810788 ≠ 0 it sets +0x28 = 0 and starts
    script 0x828C70. It then sets +5 = 1 and D_008106C8 &= 0xF1FFFF8F, and
    calls 001D2830(8, 1), 001C1DC0 and 001FABB0.
  - **Phase 1:** when 001BA1F0 returns non-zero, it sets D_008106C8 |= 0x40
    and calls 001D2830(8, 0), 001C1DC0, 001AEE10(4, 0), 001C4760(0x1A, 1) and
    001FAE70(0). It then sets +0x2E = 0xFFFF, D_00810808 = 0xFF and +4 = 3.
  - Every lifecycle-1 tick ends with 001B17A0.
  - **2 and 3** free; **4 and above** return with no call.

## 3. Verification (`tools/test_script_door_fan_reference.py`)

The oracle is `FallEE` (the shared EE interpreter with COP1 routed through
`tools/ee_float_model.py`). It runs over copy-on-write views of the captured
RAM. Every callee is a hook that records its arguments and is answered from a
per-case script. The same script answers the native workers. Every case
compares:

- the result;
- the ordered calls with their arguments;
- every modelled record/global byte;
- **every original write**, which must lie inside the modelled bytes.

The native build uses `-Wall -Wextra -Wpedantic -Werror -ffp-contract=off`. A
ctypes/C layout probe checks every struct.

### 3.1 Part 1: the seven em_area_script handler rows

The lockstep of `test_area_script_reference.run_scenario` is rerun. Every
comparison of that test applies, and none of its code is changed. The runs
are:

- 12 of its level scenarios (Roger 0x8283D0 with skips, 0x828990, 0x828810,
  0x828A10, director 0x8294C0 with skips, and the synthetic flags, fades,
  skippable and face scripts);
- 34 generated scripts for the handler paths the level scripts never reach:
  - op16 while a frame is open;
  - op06 sub2/sub4 waits and subs ≥ 7;
  - op10 subs ≥ 10 and the sub4 wait;
  - 001B81D0 with D_0081078F = 1, models 0x3E/0x3F/0x40/other, and a refused
    001CA700;
  - op15 with the owner on either side and at both yaws, zero and non-zero
    turn rates, the message slot freed early, and the animation bit set early.

These are test data in an enlarged synthetic arena, not game data.

This test records the original instructions fetched inside each handler.
Across the full set, **every instruction of the seven handlers executes**
except 18, and each of the 18 is listed with its reason (`UNEXECUTED`):

- 10 are dead instructions: they follow an unconditional branch's delay slot
  and no branch targets them;
- 8 sit on phase values ≥ 2 or ≥ 5, which the handlers never write.

The totals are:

| Handler | 001B6BF0 | 001B6E40 | 001B6F80 | 001B6FA0 | 001B7840 | 001B81D0 | 001BA080 |
|---|---|---|---|---|---|---|---|
| executed | 93/96 | 21/22 | 6/6 | 251/255 | 117/122 | 57/62 | 69/69 |

Quick mode runs a covering subset of 17 scenarios, chosen by a greedy cover of
the full coverage and hardcoded in `QUICK_COVER`. Its coverage assertion is
identical. Full mode runs all 46.

### 3.2 Part 2: em_script_door_fan.c

| Function | Default / full cases | What varies |
|---|---|---|
| 001BA510 | 6 / 40 | random neighbours; only D_008106D4..DF may be written |
| 001BAC00 | 15 / 15 | the opening's own list 0x828F30 from the captured overlay (both spawn, one or both allocations fail); generated lists: commands 0/3/5/−1 × handler 0/non-zero, 0x270E with and without a node, negative bank indices, a first entry whose short is −1, four entries with one failed spawn |
| 001BAD40 | 71 / 299 | messages 0x270D/0x270C; commands 0..9, 0x7FFF, −1 × bone counts 0/2/3/5/0x78 × budgets −1/0/2/3/0x40/0x78 × 001C61D0 results × the 001C5C90 lifecycle; both opening entries |
| 001B1B30 | 7 / 7 | +1 in, 001B1630 0/1, denormal and −0 arguments |
| 001BC240/001BC290 | 40 / 90 | +0xB, 001C64F0 result (including 0x8000), B8 0/1/2 |
| 001BBD60 | 48 / 515 | all 256 rows × sides 0/1, negative link, sides 2 and 0xFFFF; the rows the doors read equal the user's ELF file |
| 001B0080 | 20 / 40 | fixed seat (1, 4) and the four non-fixed gate combinations with random player, angles, length and a1; 001B1470/001029C0/00102C58/001026A0 run as original instructions (VU0 through the measured model), and their logged results answer the native workers with inputs asserted |

Captures:

- **Opening capture** (`opening_ee.bin`). The native 001BAC00 runs over the
  captured overlay list and nodes. Every field it writes equals what the
  capture holds on the two spawned actors 0x7A96E0 and 0x7AE920 (18
  fields). The second actor's +0xB0..+0xC8 are excluded: 001C5C90, its
  command-5 handler, rewrites them every tick (its decomp C stores them).
- **Opening capture, 001BAD40.** The native 001BAD40 runs on the first
  actor's entry. It reproduces the captured +0x40, +9 and +0xC.
- **Route beat 09 (fence door).** Over the 66 door rows in phase 4 or 5:
  - there is exactly one 001BC240 commit;
  - 001BC290 returns 1 exactly on the row where the capture resets the phase,
    with the captured +0xB.
- **Beat-09 snapshot.** The native 001BBD60 over the door's +0x56 and side 0
  gives the patched open-script sound word at 0x24DC58 (0x401). The ELF has 0
  there. The capture's 0x24DC14 = 0x45 shows 001BBE40 took the side-0 arm,
  which sets +0x2E = 0 first.
- **Fail-stop.** A missing worker faults at its address before the step it
  guards, and a fault latches. An entry outside the image faults. A bone count
  past the +0x110 array faults before +0xC is written. The husk trio faults
  on a missing 001B17A0.

### 3.3 Part 3: em_script_door_fan_husk.c

- **Captured states.** Before running anything, the test asserts that the
  three functions' bytes in every image equal the user's
  `extract/OVERLAY/AREA11.BIN`. Every captured AREA11 RAM image is then
  ticked once for all three owners, from the captured node: 24 images in full
  mode (the 9 startup-reference captures other than the opening, and the 15
  route beats), 4 by default.
  Every image shows the creature at 0x64, the partner at 1 and the manager at
  1/phase 0, all with D_00810788 = 0. That is the first-visit path.
- **Unit cases** (289 full, 150 default):
  - creature setup: flag 0/1/0xFF × 001B0FD0 0/1 × rand extremes × a child or
    none;
  - every other lifecycle;
  - lifecycle 2 over angles below, above and exactly at −π/2 × steps ×
    countdowns × +0x224 × flag;
  - the partner and manager state tables in full.
- **Multi-tick lockstep runs:**
  - creature setup → dormant, then the flag rises to 1 and to 0xFF, entering
    lifecycle 4;
  - creature swing to −π/2 and countdown (20 ticks);
  - partner setup → shot → fall with sound 0x427 at 10 (16 ticks);
  - manager wait → start → poll → end → free.
- **Untranslated lifecycles.** For lifecycles 1 and 4 the oracle hooks the
  original entries 0x826190 and 0x825B74. The test asserts that the original
  reaches exactly the entry at which the native faults.

### 3.4 Branch coverage and mutations

Every conditional branch inside the translated ranges is observed both ways.
The ranges are 001BA510, 001BAC00, 001BAD40, 001B1B30, 001BC240, 001BC290,
001BBD60 and 001B0080; the creature's 0x825940..0x825B74 and
0x826D60..0x826F2C; the whole partner and the whole manager. The test asserts
this in both modes, with no exceptions.

Each of these mutations fails the default run:

- 001BAD40 mode `< 3` → `<= 3`;
- the 001BAC00 default-handler test on +0xA != 4;
- 001BC290 without the +0xB clear;
- the creature timer shift;
- partner `< 10` → `<= 10`;
- the swing angle with host float arithmetic;
- the manager mask;
- 001B0080's +17, a dropped wrap, the host-float y add, and a zeroed vector
  length.

## 4. Timing

The default run takes about 3 s: part 1 ≈ 1 s on 8 workers, parts 2 and 3
≈ 2 s. `EM_TEST_FULL=1` takes about 60 s.

## 5. Binding (for the coordinator)

General: one `EmSdfWorkers` / `EmHuskWorkers` per owner, with `ctx`
identifying the actor. The adapters map the views onto the canonical storage,
call the function, and write the view back. The fault records use the same
codes 1 (NULL) and 2 (worker failed) as `EM_SCENE_FAULT_*`. The remaining
codes are listed in each header.

### 5.1 L19

- **The seven handler rows** need nothing new. They go live when
  `em_area_script` is bound (AREA_SCRIPT.md section 6).
- **001BAC00 (op14).** Two live places run op14, and both need changes.
  - `em_opening_runtime.c` case 20 accepts the opening's list and does
    nothing: a stand-in. Replace it with `em_sdf_001BAC00(owner, record,
    image, w, fault)`, where:
    - `owner.self_14` is the opening owner node (0x823E80's record) and
      `s2E` its +0x2E;
    - `image` is the AREA11 overlay arena (0x823500..0x82AD00) or the
      EMSC image holding 0x828F30;
    - `w_001AFA90` is the `em_actor_pool` alloc (the 001AFA90 translation),
      returning the record's original address and a view onto its fields;
    - `w_001C8140` is not reached by the opening list (fail-stop otherwise);
    - `r_0028A490` reads the bank table.
  - Or admit op14 in `em_area_script` by calling the same function.
  - **Dependency.** The two actors get +0x10 = 001BB0E0. Their behaviour is
    `em_slg_001BB0E0` (lane L34, `em_startup_load_gaps.c`). Binding
    001BAC00 before that is bound would spawn nodes that fault on their first
    tick. Bind both together, and retire the prepared-pose path of
    `em_opening_actor` for these two actors at the same time.
- **001BAD40.** Bind `EmSlgScriptActorWorkers.w_001BAD40(ctx, a, &ret)` to
  an adapter:
  - build `EmSdfEventActor` from `a->node` (+4, +9, +0xC, +0x18, +0x40, +0x44
    and +0x110..);
  - call `em_sdf_001BAD40(view, a->entry, world, w, fault)`, write the view
    back, and set `*ret` to the result.
  - `world` points at the message request words (the `EmMessageService`
    block), D_008106C0, the camera object's +0x70/+0x74/+0x78/+0x04/+0x6E
    (the same storage as `EmAreaScriptWorld.cam_70/74/78`, d8101E4 and
    cam_6E) and D_00275BCC (the bone budget the pool owns).
  - The workers are:
    - 001CA6E0 → `em_player_face_host`-style bind (it must set +0x44);
    - 001C6120 → the bank/index resolver (em_cinematic_camera);
    - 001C5C90 → the equipment child tick (L22, UNBOUND today);
    - 001AF780 → the pool's bone-slot pop;
    - 001BA8E0, 001D8BF0, 001CA6F0 → the L22 translations;
    - 001CB5B0 → `em_pose_host_workers` (anim_bone_array_setup);
    - 001C63E0 → bone_init_default_2;
    - 001C61D0 → the bank clip length;
    - 001C67E0 → anim_clip_init;
    - 0022EC30 → `em_cinematic_playback_start`.
- **001BA510.** `em_interaction_frame` already carries it inline. Other
  callers bind `em_sdf_001BA510(EmSceneState.req + 0x24, fault)`.
- **Verified-unbound rows.**
  - `em_script_host_workers`: its slot table in SCRIPT_HOST_WORKERS.md
    section 3 covers 00182BF0, 001B0460, 001B12B0, 001B1380 and 001B6250.
    **001B0460's worker `w_001B0080` binds to `em_sdf_001B0080`** (section 5.2).
  - 001B0B50 → `em_player_closure1019_001B0B50`, reached through
    `w_001B0B50` of `em_script_host_001B0460`.
  - 001B15D0 → `em_player_misc_001B15D0`.
  - 001B7B30, 001B8020, 001B8FC0, 001BA1A0 are the `em_area_script`
    handlers and start (AREA_SCRIPT.md section 6). 001B7B30 sub0 goes to
    `em_cinematic_playback_wait`. Their stand-ins, per the census, are the
    em_camera.c retarget/chase hooks, the opening/cinematic camera track and
    the em_script.c / em_truck.c legacy starts.

### 5.2 L18

- **001B0080.** Bind `EmScriptHostWorkers.w_001B0080(ctx, camera, a1)` to an
  adapter:
  - `camera` must be 0x8101E0. The `EmSdfSeat` view is camera +0x0C,
    +0x10..+0x3C (the same storage as `EmAreaScriptWorld.cam_0C/cam_10/
    cam_20` and the rotation quad).
  - The `EmSdfSeatWorld` fields are: `d810700`/`d810702` =
    `EmSceneState.d810700/d810702`; `d810350` = the player +0xA0 quad;
    `spad3B50` = scratchpad 0x70003B50; `d8105D0`/`d8105E0` = the camera
    working eye/target; `spad3400`/`spad3600` = scratchpad.
  - The workers are `w_001B1470` = `em_player_001B1470` (its |x| < 4096
    domain; the angles here are far inside it) and the VU0 leaves 001029C0,
    00102C58 and 001026A0 (the same leaves `em_camera_area11_specials` and
    `em_camera_follow_original` take).
  - It replaces the legacy door camera re-seat (CAM-07) and
    `em_game_legacy_camera_rearm`, which is reported as `UM_001B0460`.
- **001B1B30.** In the `em_door_original` publish hook
  (`EmDoorOriginalHooks.publish(ctx, point)`), call
  `em_sdf_001B1B30(&door->visible, point[0], point[1], point[2], w, fault)`.
  Its workers are 001B1630 (the in-cone test, L35) and 001B1B70. The hook
  returns +1. The call must also run 001B1B70, which the hook contract did
  not name before.
- **001BC240 / 001BC290.** `em_door_original` phases 4 and 5 already
  translate them inline (verified). The standalone functions are for callers
  that keep the original call shape. Nothing more to bind.
- **001BBD60.** In `EmDoorTransitHooks.patch` (the 001BBE40 script patch of
  0x24DC40), write the record's +0x18 (0x24DC58) with
  `em_sdf_001BBD60(door +0x56, door +0x2E, &word, w, fault)`.
  - `r_0024DB80` reads the user's ELF halfword. `em_door_original_runtime`
    today carries an offline copy of the pair (metadata + 68) that
    `em_door_transit_prepare` indexes by side.
  - This replaces that offline export and em_door.c's legacy door sound
    patch. The route-09 capture pins the result (0x401 for link 0x400,
    side 0).
- **Verified-unbound rows.**
  - 001BC350/001BBDA0/001BC0E0/001BC300 → `em_door_original_tick`, in place
    of `em_area11_bindings.c` `tick_door` (legacy `em_game_legacy_door_tick`).
  - 001BBE40 → `em_door_transit_kickoff`, in place of em_door.c's walk-to at
    15 u/s (audit H13).
  - 001B94F0 → `em_area_script` op01.
  - DOOR_ORIGINAL.md gives the runtime resources.

### 5.3 L24

- **Fan 0x827630.** Follow FAN_ORIGINAL.md "Binding". It replaces the NULL
  row "fan: static" of `em_area11_bindings.c` (em_pickup's static draw).
- **Creature 0x825940.** It replaces `tick_enemy_00825940`, which is the
  legacy `em_enemy_update` group through `em_game_legacy_enemy_tick` plus its
  state-0 inline child spawn (0x825A74: 0x7A, (0, 0, 0, 0.25), a 001C5680
  node of its own; the translation must write that child's +0xA0 through the
  node the spawn keeps at its +0x220, em_area11_bindings.c).
  - Call `em_husk_creature_tick(view, world, w, fault)` per pool tick.
  - The view maps +0, +4, +0x28, +0xB0..+0xCC, +0x11C and +0x1F4..+0x224 of
    the record.
  - The world maps `d810758` to `EmProgress` (D_00810758..; 0x30 is
    D_00810788) and `spad3A20` to scratchpad 0x70003A20. The manager uses
    `d8106C8` and `d810808`.
  - Workers:
    - 001B0FD0 → the model/bone setup (as `em_fan_original`'s w_001B0FD0);
    - 00122BB8 → the game rand (`em_random`, shared state);
    - 0011E2A8 → `em_area_script_w_0011E2A8` (verified to |x| ≤ 3π/4;
      setup passes 0 and the swing keeps +0x1FC within [−π/2, 0]);
    - r_00275B40 / s_bone_f32 → the current bone work array (the pool walk
      sets D_00275B40 = node + 0x110 before the behaviour; em_actor_pool
      notes);
    - 001A2370 → the actor-hull re-transform (L07);
    - 001AFA90 → `em_actor_pool` alloc of class 0xC, returning the child's
      view. The child's +0x10 = 001C5680 must be bound to the indicator
      light node. The creature spawns it itself: **delete the interim
      001C5570 spawn** when this binds;
    - r_child_220 → the record at +0x220;
    - 00102958 → the matrix copy;
    - 001C6380, 001B17A0, the draw, and 001AFC10.
- **Partner 0x827490.** It replaces `tick_enemies` for this node.
  - `r_link_18` returns the view of the record at +0x18 (in AREA11, the
    creature node): lifecycle +4 and +0x21C.
  - Workers: 001B11E0/001B1190 → the taken-bit test/set over
    `EmProgress` D_00810860 (em_pickup's migrated taken bits); 001EFE00 → FX
    spawn (L26); 001FBD50 → sound (cue 0x426 and 0x427, range 300).
  - This restores the partner's persistent taken bit and its shot reaction.
    The audit W17 residue lists both as missing.
- **Manager 0x823CE0.** It replaces the NULL row "manager: dormant".
  - Workers: 001BA1A0/001BA1F0 → an `em_area_script` host on this node's
    +0x1F0 block, with the script image holding 0x828C70; 001D2830,
    001C1DC0, 001FABB0, 001AEE10, 001C4760 and 001FAE70 → their
    `EmSceneWorkers` bindings (001C4760 = `em_director_original_001C4760`).
  - In the first visit it only runs its lifecycle 0 → 1 step and 001B17A0
    every tick. The script path is revisit content.
- **Remove the husk pair from `em_enemy`'s aggregate** when binding, so no
  node runs twice.

## 6. Limits and open items

- **Creature lifecycles 1 and 4 are not translated.** They span
  0x825B74..0x826190 and 0x826190..0x826D60, about 1,150 instructions of the
  active creature. Their callees include the overlay helpers 0x826F70 and
  0x827440, the probe 0019B6C0, 001EFD90 and the SDK trig.
  - They are entered only after D_00810788 == 0xFF.
  - In AREA11 only the manager's script 0x828C70 could raise that flag, and the
    manager starts that script only after something else has set the flag
    non-zero. The only record that sets it to 1 is in AREA17.BIN (FIRST_LEVEL_AUDIT
    INV-08 and W17).
  - All 25 captured AREA11 images show the flag at 0.
  - So they are revisit content. The native faults at their entries rather
    than substituting behaviour, and the oracle proves the dispatch.
- The manager's script 0x828C70 has no export and is not run here.
  001BA1A0/001BA1F0 are workers.
- 001BAC00's 0x270E path (001C8140) is covered only by generated lists. The
  opening list has no 0x270E entry.
- The route census does not give per-case coverage for 001BAD40's command
  switch. The opening capture shows commands 0 and 5 only (the two entries of
  0x828F30).
- Worker domains are the callers' responsibility: 001B1470 (|x| < 4096) and
  0011E2A8 (|x| ≤ 0x4016CBE3).
- Part 1 depends on `test_area_script_reference.py`. It reuses that test's
  lockstep and scenarios, and patches only its `Env` (a 001CA700 result
  override) and its synthetic-arena size, inside this process.

## 7. Changes needed in existing files (lead / coordinator)

- **Makefile.** Add the test targets below. When a binding lands, also add
  `src/game/em_script_door_fan.c src/game/em_script_door_fan_husk.c` to
  `COMMON`.
- **Rebinding.** `em_opening_runtime.c` case 20 (op14) and
  `em_area11_bindings.c` (the rows 0x825940, 0x827490 and 0x823CE0, and
  `tick_enemy_00825940`'s child spawn) are rebound as section 5 describes.
  `EmScriptHostWorkers.w_001B0080` binds to the new adapter.
- **Census.** Apply the corrections of 1.4 and the after-statuses of section 1
  in the next classification pass.

```make
.PHONY: test-script-door-fan
test-script-door-fan:
	mkdir -p build && $(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -ffp-contract=off -Isrc tests/script_door_fan_test.c src/game/em_script_door_fan.c src/game/em_script_door_fan_husk.c -lm -o build/script_door_fan_test && ./build/script_door_fan_test

.PHONY: test-script-door-fan-reference
test-script-door-fan-reference:
	python3 tools/test_script_door_fan_reference.py
```
