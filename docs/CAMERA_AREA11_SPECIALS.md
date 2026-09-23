# Camera area specials, event router, lock-on and aim release

Original executable SHA-256:
`ee052236783e7d3e865754d3ff9fee71290addeb7d146c86caa7ff2724d1e17a`.

Lane "camera-area11-specials". This document covers four original routines
of the gameplay camera and the leaf they share:

| routine | what it is | audit item |
|---|---|---|
| 00195130 | camera action 0 of mode 0: the per-area walking camera. AREA11 is area 0xB. | CAM-09 |
| 00193EB0 | the event router on the player's action code, including the L1 orient-behind gate. | CAM-11 |
| 001936E0 | camera action 3 (both modes): the melee lock-on swing. | CAM-11 |
| 00197490 | the aim release, called by the aim actions 1 and 2. | CAM-16 |
| 00191210 | the area-0x10 eye clamp. All four routines call it. | |

It covers what each routine does, the translation
`src/game/em_camera_area11_specials.c/.h`, the evidence, and how the
coordinator binds it. The module is **built and tested but not wired**.
Section 4 lists what to bind. Section 5 lists the workers that have no
translation yet.

### What the route shows

The route captures (`build/s87/route`, FIRST_LEVEL_ROUTE.md) show this:

- **The camera action is always 0 on the route.** `cam_mode` is bytes
  +4..+7 of the camera block. +6 is 0 in every row of all 15 beats, and +5
  is 0 too. So 0018BC20 runs 00195130 on every gameplay frame, from the
  panel (beat 00) to Roger (beat 14). The lock-on (001936E0) and the aim
  release (00197490) are never reached on the route. Their only evidence is
  the unit oracle and the world variants in section 3.
- **Every route snapshot is in AREA11 in walking-camera state 1.** In every
  `eeMemory.bin`, the camera block has +1 = 1 and +6 = 0, D_00810700 = 0xB,
  and the player's action code +230 is 1.
- **Neither AREA11 arm fires on the route.** The first arm needs y < 185,
  z < 220 and 359 < x < 394.8. The only rows inside that XZ box are in beat
  14, frames 257 onward, and there the height is 295 or more. The second arm
  needs to be within 8 units of (321.5, 216.7). No row comes within 20
  units. So on the route, 00195130 in AREA11 runs this sequence:
  1. 001916C0(cam, player, 0);
  2. no arm;
  3. 001921D0(cam, player, 0);
  4. 00191210 (a no-op outside area 0x10);
  5. 00193EB0(cam, player, 0). Code 1 with +5 = +6 = 0 calls
     00191000(cam, player), the L1 orient-behind, on every such frame.

  World mode (section 3) confirms this sequence on all 15 snapshots.

## 1. What the original does

The camera block is the 0xD0 bytes at D_008101E0. 001CB590 clears it. The
player record is D_008102B0 (0x320 bytes). All offsets below are from those
two addresses. "eye" is D_008105D0 (vec4) and "target" is D_008105E0. The
addresses in brackets are the instructions translated there.

### 00195130(cam, player): camera action 0

It dispatches on the state +1:

- **0.** Set +1 = 1, clear +2, +3 and the halfword +8, then fall into 1
  (00195194).
- **1.**
  1. 001916C0(cam, player, 0) runs first [001951AC].
  2. The area arm runs on D_00810700. The area is read *after* 001916C0
     [001951B8].
  3. If no arm took the frame ("handled" = 0), 001921D0(cam, player, 0)
     runs [00196130].
- **2.** The auto orbit: 00193D90(cam, player, 1), then 0018D7B0(cam, 0)
  [0019614C].
- **3.** The freelook hand-back: 001921D0(cam, player, 1) [00196168].
- **4.** The area-0xD pan: 001916C0(cam, player, 0), then a sequence
  driven by +2 and the countdown +8 [00196178]:
  - Step 0 arms +8 = 120 and falls into step 1.
  - Step 1 counts +8 down, easing toward (669.3, 182.3, 1082.5) (x/z at
    1.1, y at 0.6).
  - Step 2 eases toward (765.6, 279.1, 1103.4) (x/z at 0.6, y at 0.48). When
    the two chase results OR to 7, it re-arms +8 = 120 [001962C0].
  - Step 3 counts +8 down. At zero it advances and calls 001AEDE0(4, 0).
    Then it falls into step 4.
  - Step 4 eases toward (773.6, 387.3, 1136.7) (x/z at 0.6, y at 0.32).
- **Any other state** does nothing here.

Every state then runs 00191210 [00196368] and
00193EB0(cam, player, handled) [00196378].

The "ease" pair used throughout is:

- 0018C6A0(cam+10, eye, rate): the x/z chase;
- 0018C4B0(eye, cam+14, rate): the y chase.

"Fixed eye" means 00192010(cam, cam+8C + (cam+5C + p+B4), 25, far) followed
by 0018D7B0(cam, 5).

**The area arms:**

- **Area 0xB, AREA11** [00195B00]. Two arms:
  - **The box arm.** It fires when p+A4 < 185, p+A8 < 220 and
    359 < p+A0 < 394.8. The order is:
    1. 0022FCA0(cam, player, 8). a2 still holds the 8 from the area
       compare [00195B80].
    2. cam+18 = 219 and cam+14 = 220.
    3. 0018D7B0(cam, 5).
    4. The x/z chase at 1.5, then the y chase at 1.0.
    5. handled = 1.
  - **The centre arm.** It fires when the action code is 6, 7, 8 or 9 and
    the squared XZ distance from (321.5, 216.7) is under 64. The squared
    distance is formed as a multiply-accumulate. The order is:
    1. cam+10 = 366.6 and cam+18 = 216.1.
    2. The fixed eye with far = 20.
    3. For code 7, the eye becomes a 16-byte copy of cam+10 (00102948)
       [00195CAC]. Otherwise the x/z chase runs at 0.8.
    4. The y chase at 0.8.
    5. handled = 1.
- **Area 0.**
  - Codes 0x14/0x15 set cam+94 = −10 − cam+C.
  - In room 2 with D_00810803 = 3:
    1. cam+94 = 0.
    2. 00194DB0(cam, player, 8).
    3. handled = 2.
- **Area 4.** With the byte cam+6D set, cam+98 is set as follows:
  - 11.5 when p+A8 ≤ 360 or p+A4 ≤ 54;
  - otherwise 0.
- **Area 6.**
  1. 001944B0(cam, player, 0). If it is nonzero, handled = 1 and the arm
     ends.
  2. Otherwise 00194D10(cam, player, 2). If it is nonzero:
     1. 0018D7B0(cam, 5).
     2. cam+14 = 90.
     3. The y chase at 0.7. handled = 1.
     4. If the chase returned nonzero, cam+10/+18 are set to
        (−367.7, −598.4) and the x/z chase runs at 0.7.
- **Area 8.**
  - **Room 2:** 001944B0(cam, player, 8). A nonzero result sets
    handled = 1.
  - **Room 3** [0019539C]:
    - **Codes 6–9.** A "far" test holds when either:
      - the squared hip (+B0/+B8) distance from (129.7, 150.5) is under 25;
      - or p+A8 > 142 and p+A4 > 230.

      **When far is true:**
      - Above 217.1, the eye is (148.6, 129.2) and cam+14 is clamped into
        [240.7, 282.4].
      - Otherwise, the eye is (136.7, 182.0) and cam+14 is clamped into
        [146.2, 223.6].

      **When far is false:** the arm needs the squared hip distance from
      (129.8, 160.5) under 25. Then cam+14 is clamped into
      [146.2, 223.6] and the eye is (136.7, 182.0).

      Every clamp that moves cam+14 mirrors it into D_008105D4. Then:
      1. The x/z eye is published into D_008105D0/D8.
      2. The fixed eye with far = 10.
      3. The y chase at 0.8.
      4. handled = 1.
    - **Code 0xA,** when z > 150 and y > 230:
      - **z ≤ 168:** the eye is (148.6, 129.2) and cam+14 is capped at
        282.4. 00192010(.., 25, 10) runs.
      - **Otherwise:** the eye is (131.7, 205.2) and cam+14 is 287.2.

      Then 0018D7B0(cam, 5), the y chase at 0.8, and handled = 1.
    - **Other codes,** when p+A4 < 217.1:
      1. The eye is picked:
         - (131.1, 182.5) when x < 145.3 and z < 163;
         - otherwise (167.8, 180.9) when z < 163;
         - otherwise (131.1, 182.5) again when cam+10 already equals 131.1
           [001954B4], else (167.8, 180.9).
      2. The limit is −20 when cam+64 = −46.8, else −10.
      3. t = D_0081069C − |cam+C| goes to 0x70003A20.
      4. When t is under the limit, 0x70003A24 is set:
         - for the −20 limit: 0.5 · (t − limit);
         - otherwise: t − limit, floored at −10.

         0x70003A24 is subtracted from the wanted height.
      5. 00191D40(cam, 11 + (cam+8C + ((cam+5C − s3A24) + p+A4)), 4) runs.
         Without the offset, the height is 11 + (cam+8C + (cam+5C + p+A4)).
      6. 0018D7B0(cam, 5), then the y chase at 0.8.
      7. handled = 2, and the x/z eye is published.
- **Area 0xD** (index D_00810702 ≥ 8):
  - The AREA13 overlay hook 0x823FE0(cam) runs first. If it returns nonzero,
    the eye is (801.3, 282.3, 1171).
  - Otherwise, a player byte +F = 0xB sets +1 = 4 and +2 = 0 (the pan).
  - Otherwise, a nonzero 00194D10(cam, player, 0) sets the eye to
    (839.8, 198, 1217.3).
  - Otherwise, x < 663 and z < 738.5 set the eye to (650.9, 187.6, 785.3).

  Each eye choice is followed by 0018D7B0(cam, 5) and the chases.
- **Area 0xE.** 00230230(cam, player). A nonzero result sets handled = 1.
- **Area 0xF,** room 1: codes 0x14/0x15 set cam+94 = −10 − cam+C.
- **Area 0x11.** cam+98 = 20 when 19600 < d² < 42025, where d² is the
  squared distance from (340, 270).
- **Area 0x13.**
  - **Room 0:**
    1. 001944B0(cam, player, D_008106F2). A nonzero result sets
       handled = 1.
    2. Otherwise, codes 0x14/0x15 set cam+94 = −43 − cam+C.
  - **Other rooms:**
    1. 001944B0(cam, player, 7). A nonzero result sets handled = 1.
    2. Otherwise, codes 6–9 within 8 of (892.1, 929.5) set the eye to
       (874.8, 887.1). Then the fixed eye with far = 20, both chases at
       0.8, and handled = 1.

### 00193EB0(cam, player, handled): the event router

It switches on the action code p+230 [00193EBC]:

- **1 or 0x21 (the L1 gate, CAM-11)** [00193FCC]. When
  handled ≠ 2, +5 = 0 and +6 = 0, it calls 00191000(cam, player). In
  every other case it does nothing.
- **Action changes.** Each of these also sets +1 = 0:

  | code | sets +6 to | address |
  |---|---|---|
  | 0x12 | 0xB | 00193FBC |
  | 0x28 | 0xE | 00193FAC |
  | 0x29 or 0xC | 2 (aim) | 00193F9C |
  | 0xD or 0x2A | 1 (aim) | 00193F8C |
  | 0x10 | 9 | 00193F7C |

- **6, 7, 8, 9, 0x2C or 0x2D (the run codes)** [00193FFC]. In the areas
  below, a "region event" is 001B0C60(0x13, 0, n) followed by +6 = 7. It
  leaves +1 as it is.
  - **Area 0x16:** +6 = 0xC, +1 = 0.
  - **Area 0x13:**
    - **Room 0:** +6 = 0xD and +1 = 0 [00194038].
    - **Otherwise,** when D_008106B8 and the scratchpad byte 0x70003B8D are
      both 0:
      - x < 872: y ≤ 356 fires event 0xD;
      - x ≥ 872 and z ≤ 900: y ≤ 365 fires event 0xC;
      - x ≥ 872 and z > 900: y ≤ 356 fires event 0xB [001940D8].
  - **Area 0xD,** with the same two gates:
    - index 4 or 6: y ≤ 159 fires event 9;
    - index 5 or 7: y ≤ 159 fires event 0xA.
- **Any other code** does nothing.

### 001936E0(cam, player): the lock-on swing

- **State +1 = 0** [00193728]:
  1. The setup:
     - the halfword +A0 = 120;
     - +1 = 1, +2 = 0, +8 = 0;
     - cam+30 = the scratchpad angles 0x70003B50;
     - the rotation matrix at 0x70003400 = 00102C58(identity, identity,
       cam+30). The identity comes from 001029C0.
  2. The target cam+20 is the player position p+A0. Its y (cam+24) is:
     - for codes 0xF, 4 and 2 (the "small" targets), 11 + (p+A4 + cam+8C);
     - otherwise, 11 + (6 + p+A4).
  3. The matrix translation (0x70003430) becomes p+A0..A8.
  4. The offset (0, lift, −cam+4C, 1) goes to 0x70003600. lift is 9 for the
     small targets and 19 otherwise.
  5. 001026A0 transforms the offset into the eye goal cam+10.
  6. The target chases: 0018C4B0(target, cam+24, 2), then
     0018C6A0(cam+20, target, 2). Then 0018D7B0(cam, 6).
  7. The ground check:
     - **Small targets:** 00193660(cam, player) is the grab test. Nonzero
       clears +6, +1, +2 and +3.
     - **Otherwise:** 0x70003630 = cam+10 − cam+20. The XZ length is
       sqrtf(dx·dx + dz·dz) (0011E748). If it is under 7, cam+14 is kept
       in [p+A4 + reach + 10, p+A4 + 30], where reach is 9 or 19.
  8. cam+14 is clamped into [cam+50, cam+54].
  9. 0018D7B0(cam, 6), then 00191210.
- **State +1 = 1** [00193A30]:
  1. Both target chases at 2, the x/z eye chase at 4 and the y eye chase at
     4.
  2. **The swing-out.** It applies when the eye is below p+A4 + 23 and
     within 8 of the player in XZ. The distance goes to 0x70003A20. Then:
     1. a = 001B1470(atan2f(ex − px, ez − pz)). This is 0011E620 then the
        wrap. a goes to 0x70003A24.
     2. eye x = px + 8·sinf(a) and eye z = pz + 8·cosf(a) (0011E2A8 and
        0011DE90).
  3. The halfword +8 counts up.
  4. 0x700038A0 = cam+10 − eye. Its length |v| comes from 00102738 (the dot
     product with itself) and 0011E748. It goes to 0x70003A20. Arrival is
     |v| < 0.25.
  5. If p+38 > 0, +8 gains another 10.
  6. Arrival, or +8 ≥ 0x51 (signed), clears +6, +1, +2 and +3.
- **Then, in every state:**
  1. 00191210 runs [00193C18].
  2. The code decides what happens next:

     | code | effect |
     |---|---|
     | 0x21, 0xF, 2 or 1 | keeps the action |
     | 0x29 or 0xC | +6 = 2, +1 = 0 |
     | 0xD or 0x2A | +6 = 1, +1 = 0 |
     | any other | clears +6, +1, +2 and +3 |

  3. **Pad release** [00193CBC]. It applies when +6 is still 3 and the held
     pad bits (the scratchpad halfword 0x70003B80) share a bit with
     D_00810E74:
     - +1 = 0 and cam+48 = p+C4;
     - cam+4C = |D_0081069C|, kept in [7, |cam+64|];
     - cam+44 = 001B1240(eye, target x, target z).

### 00197490(cam, player, a2): the aim release

- **Codes 0x18, 0x17 and 5** skip straight to the common part.
- **The early returns.** Each of these returns at once. None of them clears
  +2, +3 or +8:

  | code | writes | then calls | address |
  |---|---|---|---|
  | 0x29 | +6 = 2, +1 = 2 | 00198440(cam, player, 1) | 00197558 |
  | 0xC | +6 = 2, +1 = 2 | 00198440(cam, player, 1), then 001912B0(player) | 00197534 |
  | 0xD or 0x2A | +6 = 1, +1 = 2 | 00197870(cam, player, 1) | 00197510 |

- **Every other code:** when the byte cam+8B is 0, the player position p+A0
  becomes the scratchpad vector 0x70003040 [00197574]. The common part
  follows.
- **The common part:**
  - **a2 ≠ 0** [0019769C]:
    1. 0019A910(cam+10, cam+20, 6) runs. If it hits, cam+20 is replaced by
       its point at 0x700031B0.
    2. 001916C0(cam, player, 2).
    3. The halfword +A0 = 80 and +6 = 0.
  - **a2 = 0 and +5 ≠ 0:** +6 = 0.
  - **a2 = 0 and +5 = 0** [001975A4]:
    1. cam+30 = 0x70003B50, cam+20 = p+A0 and cam+24 = p+B4 + cam+8C.
    2. The matrix at 0x70003400 is built as in 001936E0.
    3. 001026A0 transforms the offset (0, 0, −10, 1) into cam+10.
    4. cam+10 += cam+20, cam+14 += cam+24 + cam+5C and cam+18 += cam+28.
    5. target = cam+20 and eye = cam+10 (16-byte copies).
    6. 00191210, then +6 = 0.
  - **The ending:**
    - **+5 = 0:** +1 = 0 and cam+44 = 001B1240(eye, target x, target z).
    - **+5 ≠ 0:** +1 = 0, then 001B0300().

    Either way it then clears +2, +3 and the halfword +8 [00197718].

### 00191210(): the area-0x10 eye clamp

It runs only in area 0x10, index 0, when D_0081078B ≠ 0xFF. It raises two
values to at least 507:

- D_008101F8 (cam+18) [00191260];
- D_008105D8 (eye z) [00191290].

### Where the readable C differs from the instructions

00195130, 00193EB0, 001936E0 and 00197490 are NEARMISS C. The translation
follows the instructions (`build/asm`), and the oracle executes them. Four
places differ from the C:

- **00195130, area 8, room 3, other codes.** For the −20 limit, 0x70003A24
  is 0.5 · (t − limit) [00195584..00195598], not 0.5 · t.
- **00193EB0, area 0x13.** Room 0 sets +6 = 0xD [00194038]. The x ≥ 872
  split sends z ≤ 900 to event 0xC with the 365 height, and z > 900 to
  event 0xB [001940D8].
- **00197490, codes 0x29 and 0xC.** They return at once [0019756C,
  0019754C] and do not clear +2, +3 or +8. 00198440 receives
  (cam, player, 1), not (1).
- **001936E0.** 00193660 receives (cam, player) [00193918].

## 2. The translation (`src/game/em_camera_area11_specials.c/.h`)

- **The entry points.** Each works on the raw camera block (`uint8_t *cam`,
  0xD0 bytes) and the raw player record (`EmPlayerLiveActor`, by original
  offsets):
  - `em_cam_specials_00195130`;
  - `_00193EB0`;
  - `_001936E0`;
  - `_00197490`;
  - `_00191210`.
- **The adapters.** `em_cam_specials_action_00195130` and
  `_action_001936E0` have the shape of a 0018BC20 action slot
  (ctx, cam, player). `em_cam_specials_call_00193EB0` and `_call_00197490`
  add the int argument.
- **The world (`EmCamSpecialsWorld`).** It holds pointers to the binder's
  canonical storage for every global the routines read or write:
  - D_008101E0, which 00191210 writes as D_008101F8;
  - the eye and target vec4s;
  - D_0081069C, D_008106B8, D_008106F2 and D_00810700..02;
  - D_0081078B, D_00810803 and D_00810E74;
  - the scratchpad words (`EmCamSpecialsScratch`: 0x70003040, 31B0, 3400
    (with 3430), 3600, 3630, 38A0, 3A20, 3A24, 3B50, 3B80 and 3B8D).
- **Inlined leaves.** These are translated where they are called:
  - 0011DF78: fabsf, the sign bit cleared;
  - 00102948: the quad copy;
  - 001031E0: the xyz copy;
  - 001029C0: the identity, as VSUB/VADD through `em_ee_float.h` plus the
    lane rotations;
  - 001028D0: VSUB.xyzw;
  - 00102738: VMUL.xyz, VADDy.x and VADDz.x;
  - 00191210.
- **Workers.** Every other callee is a worker in `EmCamSpecialsWorkers`.
  Each returns 0 or a negative fault. Results come back through `*result`:
  v0, or the raw f0 bits.
- **Fail-stop.** Each entry point first checks the world pointers and every
  worker its path can reach. The path is set by the state byte and the area
  byte it dispatches on (`em_cam_specials_ready_*`). A check that fails
  returns −1 before any write, with `fault_address` set to the missing
  callee.
  - 00195130 checks the area arm again after 001916C0, because the original
    reads the area after that call.
  - A worker that returns a negative value stops the routine at once. The
    writes made before it are kept, as in the original order.
- **Arithmetic.** Every COP1 op and VU0 macro op goes through
  `em_ee_float.h` on raw bits.
- **Build.** It compiles with zero warnings under
  `-std=c11 -Wall -Wextra -Wpedantic -Werror -ffp-contract=off`.

## 3. Verification

### Unit oracle (`tools/test_camera_area11_specials_reference.py`, default ~9 s)

**What it executes.** It runs the original instructions of 00195130,
00193EB0, 001936E0, 00197490 and 00191210, and of the inlined leaves, from
the pinned ELF. It uses the fall oracle's `FallEE`: COP1 and VU0 go through
`tools/ee_float_model.py`, and no shared file is edited.

**How callees are handled.** Every other jal target is hooked, scripted per
case and recorded. The native module gets the same script through its
workers. The test asserts that the hooked set is exactly the jal-target
set: 39 targets, all hooked or translated.

**What it compares.**

- **Worker calls.** Every call's arguments, as a0–a3 and f12–f14 with the
  pointers as original addresses, must match. So must a digest of the
  whole compared state at that moment. That makes an intermediate
  difference fail even when a later write hides it.
- **Final state.** The 0x810000..0x810FFF window and the scratchpad words
  must match. The window holds the camera block, the player record, the
  eye/target and all the globals.
- **Store coverage.** Every store the original makes must land in that
  compared state (asserted).

**What else it checks.**

- **Branch coverage.** Every one of the 184 conditional branches of the five
  routines must be exercised both ways (asserted).
- **Fail-stop cuts.** A worker fails at a random call in every 5th case.
  The native must return −1, stop right there, and record that callee's
  address.
- **Missing-worker refusals.** In every 7th case, each of the 31 workers is
  missing in turn. The routine must either refuse, with that address
  recorded, only earlier calls made and no write when it refused first; or
  run identically without ever needing that worker.
- **Missing-world refusals.** Each world pointer and the scratch are made
  missing in turn, for each entry point. The result must be −1, with no call
  and no write.

**Results:**

- **Default (quick):**
  - 12,000 of 60,000 cases (00195130 5159, 001936E0 2631, 00193EB0 1566,
    00197490 2128, 00191210 516);
  - 32,406 worker calls identical, and all 184 branches both ways;
  - 1842 fault-stop cuts, 11,461 missing-worker refusals and 65
    missing-world refusals;
  - 6.1 s.
- **`EM_TEST_FULL=1`:**
  - 60,000 cases, 162,133 worker calls identical, and all 184 branches both
    ways;
  - 9197 fault-stop cuts and 57,518 missing-worker refusals;
  - 37 s.

### World mode (captured route RAM)

This mode runs on all 15 route snapshots (`build/s87/route/*/eeMemory.bin`
and `scratchpad.bin`). Each case runs twice, and the whole 32 MB RAM and the
scratchpad must be identical afterwards:

1. The original routine is run with its whole original callee tree.
2. The native routine is run in a second EE on the same snapshot. Every
   worker is bound to its **original** routine, executed there
   (`WorldWorkers`). The mirrored state is written in before each call and
   read back after.

This also checks the worker interface (argument order, pointers and result
kind) against the real callees.

- **Default run:** each snapshot's captured frame, as it is. This is
  00195130 in AREA11, state 1, code 1. All 15 pass. The original workers
  called were 001916C0, 001921D0 and 00191000, 15 each (about 2 s).
- **`EM_TEST_WORLD=1`** (about 150 s) runs these variants on every
  snapshot:
  - states 0/1 × 16 action codes;
  - the AREA11 box and centre positions × codes 1/6/7;
  - states 2 and 3;
  - the router with handled 0/2;
  - the lock-on in states 0/1 × 8 codes, plus the eye placed within 8 of
    the player (the swing-out);
  - the release with a2 0/1 × 6 codes, with +5 = 0 and with +5 = 1.

  Result: PASS, 1545 cases (00195130 795, 001936E0 270, 00193EB0 120,
  00197490 360), with RAM and scratchpad identical throughout. These
  original workers were exercised:

  | worker | calls |
  |---|---|
  | 001026A0 | 165 |
  | 00102C58 | 165 |
  | 0011DE90 | 30 |
  | 0011E2A8 | 30 |
  | 0011E620 | 30 |
  | 0011E748 | 335 |
  | 0018C4B0 | 645 |
  | 0018C6A0 | 600 |
  | 0018D7B0 | 480 |
  | 00191000 | 225 |
  | 001912B0 | 60 |
  | 001916C0 | 855 |
  | 00192010 | 90 |
  | 001921D0 | 555 |
  | 00193660 | 45 |
  | 00193D90 | 15 |
  | 00197870 | 60 |
  | 00198440 | 120 |
  | 0019A910 | 90 |
  | 001B0300 | 90 |
  | 001B1240 | 90 |
  | 001B1470 | 30 |
  | 0022FCA0 | 135 |

  Not exercised in the world run: the other areas' workers (001944B0,
  00194D10, 00194DB0, 00230230, 0x823FE0, 001B0C60 and 00191D40) and
  001AEDE0. The unit oracle covers them.

Commands (lane build dir via `EM_LANE`):

```
python3 tools/test_camera_area11_specials_reference.py                 # ~9 s
EM_TEST_FULL=1 python3 tools/test_camera_area11_specials_reference.py  # ~40 s
EM_TEST_WORLD=1 python3 tools/test_camera_area11_specials_reference.py # ~150 s
```

## 4. Binding (coordinator)

Nothing is wired. `em_camera.c` (0018BC20, `camera_mode_dispatch`) and its
stand-ins belong to the coordinator.

### Call sites

- **0018BC20, mode +5 = 0, action +6 = 0 (and out of range):**
  `em_cam_specials_action_00195130(&specials, cam, player)`. This is the
  generic gameplay camera on every AREA11 frame.
- **0018BC20, action +6 = 3, in both mode tables:**
  `em_cam_specials_action_001936E0(&specials, cam, player)`.
- **0018BC20, mode 1, the action-0 path's final call
  `00193EB0(p, e, 0)`:** `em_cam_specials_call_00193EB0(&specials, cam,
  player, 0)`. 00195130 calls the router itself.
- **00198650 (action 2)** calls `00197490(cam, player, 0)` at two sites.
  **00197D20 (action 1)** calls it with a2 = 1. Its a0/a1 are the
  camera/player it holds; confirm this when 00197D20 is translated. Both
  map to `em_cam_specials_call_00197490`.

`cam` must be the canonical camera block, the storage
`EmCameraFollowWorld.cam` also points at. `player` is the live record
`player_states_actor()`.

### World (`EmCamSpecialsWorld`)

Point every field at the canonical storage the follow camera
(`EmCameraFollowGlobals`) and the commit 0018C0D0 already use:

- d8105D0/d8105E0 = the eye/target vec4s;
- d81069C = `EmCameraFollowGlobals.d69C`;
- d810700/01/02 = the area bytes.

**The scratch.** `EmCameraFollowScratch` has a different layout that
overlaps s3400, s3600, s38A0, s3A20/s3A24 and s3B50. The binder must keep
the words it shares with `EmCamSpecialsScratch` identical across calls,
either through one storage or by copying at each worker boundary. Two
words need a specific source:

- **0x700031B0** is where 0019A910 leaves its point. The 0019A910 adapter
  must copy the segment walker's point into `spad->s31B0` after the call.
- **s3B80 / s3B8D** are the frame's pad-held and frame-control scratchpad
  bytes.

### Workers of `EmCamSpecialsWorkers`

| worker | original | binds to |
|---|---|---|
| `w_001921D0` | 001921D0 | `em_camera_follow_001921D0(&follow_world, player, mode)`. `follow_world.cam` is the same block. |
| `w_0018D7B0` | 0018D7B0 | `em_camera_follow_0018D7B0(&follow_world, style, &r)` |
| `w_0018C4B0` | 0018C4B0 | `em_camera_follow_0018C4B0(vec, y, rate, result)`. vec is the eye, the target or cam+20. |
| `w_0018C6A0` | 0018C6A0 | `em_camera_follow_0018C6A0(from, to, rate, result)` |
| `w_00191D40` | 00191D40 | `em_camera_follow_00191D40(cam, &globals, want, rate)` |
| `w_00192010` | 00192010 | `em_camera_follow_00192010(cam, y, f13, f14)` |
| `w_00102C58` | 00102C58 | `em_owner_services_euler_00102C58` or `em_effect_original_00102C58` (float-typed; adapt the bits) |
| `w_001026A0` | 001026A0 | `em_effect_original_001026A0` (float-typed) |
| `w_0011E748`, `w_0011E620`, `w_0011E2A8`, `w_0011DE90` | SDK math | `em_sdk_math_original_*` (float-typed, with the tables) |
| `w_001B1470` | 001B1470 | `em_player_001B1470` (bits) |
| `w_001B1240` | 001B1240 | `em_script_host_001B1240` |
| `w_0019A910` | 0019A910 | `em_coll_segment_0019A910(seg, from, to, 6)`, plus the 0x700031B0 copy above |
| `w_001B0C60` | 001B0C60 | `em_scene_request_area_change_001B0C60` (not reached in AREA11) |
| `w_001AEDE0` | 001AEDE0 | `em_transition_fade_out(fade, 4, 0)` (area 0xD only) |
| `w_001916C0` | 001916C0 | **no translation** (NEARMISS C; needed on every AREA11 frame) |
| `w_00191000` | 00191000 | **no translation** (byte-matched C; the L1 orient-behind, needed on every code-1 frame) |
| `w_0022FCA0` | 0022FCA0 | **no translation** (the AREA11 box arm) |
| `w_00193D90` | 00193D90 | **no translation** (state 2) |
| `w_00193660` | 00193660 | **no translation** (lock-on grab test) |
| `w_00197870`, `w_00198440`, `w_001912B0` | | **no translation** (aim) |
| `w_001B0300` | 001B0300 | **no translation** (the release with +5 set) |
| `w_001944B0`, `w_00194D10`, `w_00194DB0`, `w_00230230`, `w_00823FE0` | | other areas. Not reachable in AREA11, but the readiness check requires them only in their own areas. |

**Minimum for AREA11.** The as-captured route path needs:

- the world;
- `w_001916C0`, `w_001921D0` and `w_00191000`;
- for the box arm, `w_0022FCA0`, `w_00192010`, `w_0018D7B0`, `w_0018C4B0`
  and `w_0018C6A0`. The readiness check requires these for area 0xB even
  though the route never fires the arms.

So the walking camera cannot be bound until 001916C0, 00191000 and 0022FCA0
are translated.

### Makefile (report only; the lead edits it)

The module is standalone. The test builds its own shared library. Add
`src/game/em_camera_area11_specials.c` to the game sources when it is bound.

## 5. Limits and open items

- **Not wired.** The port's `camera_mode_dispatch` still runs its own
  stand-ins.
- **Untranslated workers** (section 4): 001916C0, 00191000, 0022FCA0,
  00193D90, 00193660, 00197870, 00198440, 001912B0 and 001B0300. They are
  faults when missing. Nothing substitutes for them.
- **CAM-09.** The route never enters the box arm (y < 185, z < 220,
  359 < x < 394.8). The ground along the snow is about 184.8, so y < 185
  holds wherever the player stands on it. But no route row is inside the
  XZ box below the tower heights. Whether the floor there is walkable is
  still a collision question: query the AREA11 EMCL at that XZ. The arm is
  translated and verified either way: 135 original 0022FCA0 calls matched
  in world mode, with positions placed in the box.
- **Lock-on and aim evidence.** They are never on the route (+6 stays 0).
  The unit oracle and the world variants are their evidence, not a route
  capture.
