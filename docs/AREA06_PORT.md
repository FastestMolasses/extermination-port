# AREA01 upper floor and AREA06: the level-7 new functions (lane A06T)

Lane A06T, 2026-09-28 (level side track). It covers the 39 functions the
level-7 route census found new (decomp `build/s87/census/a01u_delta.json`
and `a06_delta.json`, `new_functions`: 7 on the AREA01 upper floor, 32 in
AREA06; SEVENTH_LEVEL_ROUTE.md section 9). 27 had no verified port
translation and are translated here; the other 12 have verified
translations in other modules and are reused, and this lane re-runs those
translations against the original over the level-7 captures (section 3.4).
Every translation is compared with the original instructions by
`tools/test_area06_port_reference.py`. What that comparison covers is
exactly what section 3 states: the comparisons the test makes, on the cases
it runs. Nothing is bound: no port code calls these entries yet.

| File | What |
|---|---|
| `src/game/em_area06_port.h` | public API: 27 entries, the hook table (71 hooks and the +0x4C method), fault codes |
| `src/game/em_area06_port_internal.h` | memory view, fault latch, EE float helpers, one typed wrapper per hook |
| `src/game/em_area06_port.c` | 001885B0, 0019F680, 001EA210, 001EBE10, 001ECA20, 002072A0, 002072C0, 00207CA0, 00207CD0, 002079F0, 00207BB0, 00123020, 00207350 |
| `src/game/em_area06_port_node.c` | 00176180, 001A8F40, 0021A440, 00219F50, 0021A180, 00219870, 0021AE90, 00169250, 001944B0 |
| `src/game/em_area06_port_overlay.c` | AREA06 0x823580, 0x8242C0, 0x824560; AREA01 0x826950 |
| `src/game/em_area06_port_strip.c` | 001CE860 |
| `tools/test_area06_port_reference.py` | original-instruction oracle and comparison; the reuse checks |

Addresses are runtime addresses. The overlays are linked 0x40 below where
they run (0x824560 is `func_overlay_AREA06_00824520`, 0x826950 is
`func_overlay_AREA01_00826910`).

## 0. Scope: the census rows, and what already existed

A grep of the port (`src/`, `tools/`, `docs/`) for each address, before this
lane, found these; "hook only" means a worker slot or a call site, not a
translation.

| Row | Bytes | Decomp | First (census) | Port before this lane | Here |
|---|---:|---|---|---|---|
| AREA01 0x826950 | 580 | C, byte-identical | a01u_01 f413 | none | translated |
| 00219870 | 1,748 | NEARMISS | a01u_02 f854 | a comment in `em_weapon.c` | translated |
| 00219F50 | 548 | C, byte-identical | a01u_02 f854 | the AREA22 / AREA04 asset exports name it | translated |
| 0021A180 | 700 | C, byte-identical | a01u_02 f855 | none | translated |
| 001ECA20 | 220 | C, byte-identical | a01u_02 f872 | none | translated |
| 001944B0 | 2,140 | C, byte-identical | a01u_02 f969 | hook only (`em_camera_area11_specials.h` w_001944B0) | translated |
| 00176180 | 344 | NEARMISS | a01u_s0 f25 | hook only (the class-2 hull shove, "unported" in `em_player_floor.h`) | translated |
| AREA06 0x823580 | 1,420 | C, byte-identical (lane A06C) | a06_00 f1 | none (0x823580 hits are other areas' overlays) | translated |
| AREA06 0x824560 | 6,332 | C, byte-identical (lane A06C) | a06_00 f1 | none | translated |
| 001EA210 | 40 | C, byte-identical | a06_00 f37 | none | translated |
| 001CE860 | 1,656 | assembly (undecompiled) | a06_00 f39 | hook only (`em_security_gun_rest.h` w_001CE860; SECURITY_GUN.md: "no translation") | translated |
| 0021A500 | 1,468 | NEARMISS | a06_00 f39 | `em_gun_rest_0021A500` (`em_security_gun_rest.c`, SECURITY_GUN.md, oracle-verified) | **reused**, re-checked |
| 001EBE10 | 124 | NEARMISS | a06_00 f296 | none | translated |
| AREA06 0x8242C0 | 128 | C, byte-identical (lane A06C) | a06_02 f340 | none | translated |
| 002072C0 | 136 | C, byte-identical | a06_02 f369 | a comment in `em_status_page.c` | translated |
| 00207350 | 1,692 | C, byte-identical | a06_02 f370 | docs only (STATUS_PAGES.md) | translated |
| 001FD0E0 | 908 | NEARMISS | a06_02 f371 | `em_cs_001FD0E0` (`em_census_standins.c`, CENSUS_STANDINS.md, oracle-verified) | **reused**, re-checked |
| 002079F0 | 436 | NEARMISS | a06_02 f371 | none | translated |
| 001FDDB0 | 696 | NEARMISS | a06_02 f372 | `em_mpr_001FDDB0` (`em_message_presenter_rest.c`, MESSAGE_PRESENTER_REST.md, oracle-verified) | **reused**, re-checked |
| 00207BB0 | 240 | C, byte-identical | a06_02 f632 | none | translated |
| 002072A0 | 20 | C, byte-identical | a06_02 f644 | none | translated |
| 00123020 | 324 | word asm | a06_02 f780 | none | translated |
| 00207CA0, 00207CD0 | 36 each | inline asm | a06_02 f1036 / f1056 | none | translated |
| 001AFF90 | 108 | C, byte-identical | a06_02 f1576 | `em_status_scene_free_001AFF90` (`em_status_scene_original.c`, STATUS_SCENE.md, oracle-verified) | **reused**, re-checked |
| 0021AE90 | 752 | NEARMISS | a06_05 f257 | a comment in `em_player_closure_live.c` | translated |
| 0021A440 | 184 | C, byte-identical | a06_s1 f54 | none | translated |
| 001A8F40 | 188 | NEARMISS | a06_s1 f65 | hook only (`em_coll_list_passes.h` w_001A8F40, "unported") | translated |
| 00169250 | 1,104 | NEARMISS | a06_s1 f311 | none (a test lists the address) | translated |
| 0019F680 | 168 | word asm | a06_s1 f313 | none | translated |
| 001885B0 | 28 | C, byte-identical | a06_s1 f355 | hook only (a stub in `em_player_closure_live.c`) | translated |
| 00169730 | 3,448 | NEARMISS | a06_s1 f356 | `em_player_closure1019_00169730` (`em_player_closure_10_12_19.c`, PLAYER_CLOSURE_10_12_19.md, oracle-verified) | **reused**, re-checked |
| 001696A0, 001811F0, 00181430, 001814E0, 00181B80, 00181BA0 | 140, 572, 172, 584, 20, 460 | BM, BM, AW, NM, BM, BM | a06_s1 f357..f977 | inside the same translation (its static helpers, compared with it) | **reused**, re-checked |
| 00181D70 | 168 | C, byte-identical | a06_s1 f357 | `em_player_major2_00181D70` (`em_player_major2.c`, called by the closure; PLAYER_MAJOR2.md) | **reused**, re-checked |

Status abbreviations as in SECOND_LEVEL_ROUTE.md section 6 (BM byte-matched
C, NM NEARMISS, AW word assembly).

Ground truth. Where the decomp's C is byte-identical it was followed; for
the NEARMISS, inline asm, word asm and assembly rows the translation was
written from the original instructions (read locally; nothing reproduced).
For every function the order of loads and stores between calls and the
float operations (operand order, the MUL / ADDA / MADD and MULA / MADD /
MSUB distances, the `!(x <= y)` band tests) follow the instructions. Where
the decomp's text and the instructions disagreed, the instructions won. The
decomp's C of the first three was corrected to the instructions at the
round-7 close (2026-09-28; decomp docs/FINDINGS.md "NEARMISS body
corrections from the round-7 AREA06 lanes"):

- **00169250** (NEARMISS): in the odd-count branch of state 1 the original
  subtracts 0.5 (4.5 +0x2E0) (an MSUB of the half against the already
  scaled step), not 0.5 +0x2E0 as the C text read; the translation follows
  the instructions and the designed cases with odd bar lengths check it.
- **0021A440** takes two arguments: the C text named one, but the original
  passes its a1 on to 00102948 (the new end point; 00219870 passes sub or
  0x700038A0).
- **00219870** (NEARMISS): 001B0FD0 and 001AFC10 get self (the text passed
  the state byte).
- **001CE860** (undecompiled, no C text): its depth key is 001CCF70(the
  point array), the first point's; the 64 bytes 0x70003A40 copied to 0x70003400 are the matrix the
  width vector goes through (001026A0).

## 1. Interface (the AREA22 / AREA04 design)

The design is `em_area22_port.h`'s (docs/AREA22_PORT.md section 1) with:

- **Entries.** 27, named `em_area06_port_<address>`. Without a result:
  00169250, 00176180, 001A8F40, 001EA210, 001EBE10, 001ECA20, 002072A0,
  002072C0, 00207350, 002079F0, 00207BB0, 00207CA0, 00207CD0, 00219870,
  00219F50, 0021A440, 0021AE90, 001CE860, 0x823580, 0x824560. With a
  result (the original's v0): 00123020, 001885B0, 001944B0, 0019F680,
  0021A180, 0x8242C0, 0x826950.
- **Stack frames.** 00176180, 00207350 (through 00207BB0), 00207BB0,
  00219870 (through 0021A180), 0021A180, 0x824560 and 001CE860 keep locals
  or pass addresses in their frame; they take `sp`, the original stack
  pointer at entry, and address the frame at the original's offsets (the
  caller maps [sp - 0x100, sp)). Nested translations get the original's
  inner sp (00207350 calls 00207BB0 with sp - 0x30, 00219870 calls
  0021A180 with sp - 0x30).
- **Hooks.** 71 boot-function hooks named by original address, plus
  `w_callback(ctx, function, actor)` for the actor's +0x4C method (00219870,
  0x824560). 00207E40's last argument is the whole 64-bit register (a
  texture word loaded with a doubleword load).
- **Calls between translations** are direct: 00219870 -> 00219F50 /
  0021A440 / 0021A180; 00207350 -> 002079F0 / 00207BB0 / 002072A0 /
  00123020 / 00207CA0 / 00207CD0; 00169250 -> 0019F680 / 001885B0; 0x823580
  -> 001EA210. Every one is also an entry.
- **Quadwords.** A quadword load or store (lq / sq) is made as its two
  doublewords, low then high; a VU0 quadword (lqc2 / sqc2) as its four
  words in lane order (the granularity the test's EE core has).
- **Fail-stop.** As AREA22: fault 5 (unmapped or misaligned address, or
  `bytes` NULL), 1 (reached NULL hook or NULL w_callback), 2 (a hook
  returned < 0), 6 (em_ee_float.h refused a VU0 form, or a clip lane with
  exponent 255 in 001CE860). After a fault no hook runs, `bytes` is not
  called again, writes are dropped, the first fault is kept, no result is
  written and the entry returns -1; a fault latched on entry, a NULL hook
  table, a NULL fault pointer and (entries with a result) a NULL result
  pointer return -1 at once.
- **EE arithmetic.** `em_ee_float.h` on bit patterns: add / sub / mul /
  div, MADD / MSUB with the accumulator, CVT.S.W, NEG, the compare keys; the
  VU0 forms of 001CE860 (the MULA / MADDA / MADD transform chain, VDIV
  (3, 3), the Q multiplies, the SUBbc, the fog lane's MULAbc / MADDbc /
  VMINIbc / VMAXbc, VFTOI4) are the measured forms AREA00's 001CD940 also
  uses.

## 2. What each function does (behaviour, from the code)

player = the player block 0x8102B0; cam = the camera block 0x8101E0; eye =
D_008105D0. Floats are carried as the original's bits.

**AREA01 upper floor.**

| Entry | Caller (route) | Behaviour |
|---|---|---|
| 0x826950 (self, st, prm) | op09 callback of script 0x82B590, record 0x82B650 (the catwalk event, a01u_01) | by st +4: **0** prm +0x20 = player +0xA0, st +4 + 1, prm +0x10 = 0, player halfword +0x1F2 = 2, +0x25C = 2, +0x1F8 = 4.0; returns 0. **1** player yaw +0xC4 = 001B12B0(001B1240(player +0xA0, prm +0x30, prm +0x38), yaw, 4 degrees); unless it reached the goal return 0, else st +4 + 1 and on as 2. **2** prm +0x10 not below prm +0xC: player +0x1F2 = +0x25C = 0, +0x1F8 = 4.0, returns 1; otherwise sound 0x4A when prm +0x10 is 20, 40 or 55, prm +0x10 += 1, t = prm +0x10 / prm +0xC, st +0x10 = prm +0x30 - prm +0x20, 0x70003600 = prm +0x20 + t st +0x10 and 00182F90(player, 0x70003600); returns 0. |
| 00219870 (self, sp) | node behaviour of AREA06's deferred g[11] (model 0x30), loaded with AREA06 (a01u_02 f854) | **0** once 001B0FD0 is 0: +0x34 = 1, +0 = 1, 001C6380, 00219F50 (the strip geometry), +0x2C4 = 20, the object +0x2CC = 001C5570(self, (1, 0, 0, 0.25), 0x7C, 1); when the taken bit (+0x9A + 1) is set (001B11E0) the node starts as spent (+0x28 = 0, +5 = 2, 0021A440). **1** a pulse on +0x2CC's colour (+0x2A & 0x7F folded, 2 v / 128); sub 0: when the byte *D_008102C8 is not 1 and 0019AA80(sub, sub +0x10, 0x40) hits: sound 0x429, +0 = 2, +5 = 1, and a drift (0, 0, 0.2) through **D_00275B40 +0x90 stored in +0x2E4 / +0x2E0; a hit flag +0x36: 001B1190((+0x9A + 1) & 0xFF) (the taken bit), sound 0x42A, +5 = 2 and the strip end moved to the nearest point of the line toward the player; sub 1: +0x28 counts to (+0x2C4 + 10) >> 2, then sound 0x42B and (+0 == 2) +0x2C4 >>= 2, state 2; then the drift added to (*(D_00275B40 +4)) +0xC0.. while +0x2D8 is set, +1 = 001B1630(position), the +0x4C method when visible (001B1B70 first when +5 < 2 and *D_008102C8 == 1), and 0021A180 (the strips). **2** sparks 0x8000006D along (+0x2D4, 0, +0x2D0) from the 4th frame (sound 0x42C), then state 3 (and 001B1190(+0x9A), (+0x2CC)->+4 = 3) when +0x2C8 < 6 +0x2A or +0x2A == 8. **3** 001AFC10. |
| 00219F50 (self) | 00219870 state 0 | sub = self +0xB0; the matrix 0x700036A0 = self +0xD0; the far point (0, 0, 100) through it; +0x2D4 / +0x2D0 = 6 x / 6 z of the direction; sub +0x10 = 0x700031B0 when 0019A570 hits, else the far point; the matrix row 0x700036C0 scaled by the length; 001A2370(self, matrix); sub +0x30 = +0x2C8 = the length; sub +0x3C = 0, +0x38 = 1.0. |
| 0021A180 (self, sp) | 00219870 state 1 | by +0x22C: 0 one strip (sub to sub +0x10), 1 two strips (sub +0x20 to sub and to sub +0x10) and the fade sub +0x38 -= 1 / (float)+0x2C4 (sub +0x3C = 2 below 0); 2 returns 0; others 1. A strip: the direction matrix (001CD390, 00102918), h = 001CCF70, D_00266920 = 0x40, D_002668A8 = the length, 001CFA60(frame local, matrix, sub +0x38, 0), 001CFBE0(h, 4, D_002668A0, local, 1). |
| 0021A440 (self, src) | 00219870 | sub +0x20 = src; sub +0x30 / +0x34 = the distances of sub / sub +0x10 from it; sub +0x3C = 1. |
| 001ECA20 (a0, a1) | the AREA06 load (a01u_02 f872) | two 001CFB50 / 001CFBE0 pairs on D_0081F8F0 (view floats *D_00275C34 +0x54 / +0x5C, 1.0, 1e-6, 0.0) with the register tables 0x256DC0 (mode 2) and 0x256E50 (mode 1). |
| 001944B0 (cam, ent, idx) | camera dispatch 00195130 (the AREA06 arrival, a01u_02 f969) | camera states 29..37 by ent +0x230 (jump table), preset row D_0024A530 + 20 idx: **29** idx >= 7: cam y from the row, yaw toward ent, the eye chases (0018C6A0 / 0018C4B0 0.8), 0018D7B0(cam, 5); idx < 7: position from the row, yaw, 0018D7B0, eye = position. **32 / 33** (and **36 / 37** in area 8 or 0x13 sub 1): t = D_00810690 - \|cam +0xC\|; t > 0 moves the camera by t along the xz direction to cam +0x20, below -30 (cam +0x64 == -46.8) / -20 by t minus that limit; 0018D7B0(cam, 4), cam y from the row, the chases. **30** the yaw steps (pi / 600) toward the row's base yaw -/+ pi/6 by the side of ent's facing; **31 / 34 / 35** (and 36 / 37 elsewhere) the yaw from 00191120; the camera on the orbit of D_008105E0 / E8 at cam +0xC + +0x94; clamps x >= -365 (idx 0), x >= 749.5 (3), z >= 1010 (6); the chases and 0018D7B0(cam, 5). Returns 1 for 29..37, else 0. |
| 00176180 (actor, a1, target, sp) | the class-2 hull shove after a mask-bit-0 hit (a01u_s0 f25) | unless +0x1F0 == 0x3C: p = actor +0xB0 + D_00281B50, the probe p + 4.5 n (n its normal) at target's y, 0019AFE0(actor, target, probe, 6) adds the scratchpad offsets 0x700031C0 / C8; actor +0xB0 / +0xB8 = p; 0x700031F0 = 1 for +4 == 1 and +5 == 0x1D. (0011E748 of D_00281B50's length is called and its result unused.) |

**AREA06.**

| Entry | Caller (route) | Behaviour |
|---|---|---|
| 0x823580 (self) | placement [9] (the node 0x7ACEB0), every frame | three countdowns at +0x1F0; sub 0 sparks class 0x80000042 at 0x826A70 every rand % 50 + 50 frames with sound 0x41D (001F02C0), and after 240 frames sub 1 and 0019C6F0(0xD, 1); sub 1 the same sparks every rand % 5 + 15 frames, every rand % 2 + 2 frames a strip class 0x8000003B from 0x826A70 to a random point (-225 - 30 r, 20, -595 - 170 r') given its length and 0.4, a random sound 0x41D..0x41F on every 100th frame counter, 001EA210(2.0) on even rand and w0 % 3 == 0, and after 90 frames sub 0 and 0019C6F0(0xD, 0). States 2 / 3: 001AFC10. |
| 001EA210 (value) | 0x823580 | 001D2DE0(2, 001281C0(value)). |
| 0x824560 (self, sp) | the beam [11] (node 0x7AD490), every frame | see the header comment of the file; REC = its placement record. **0** the pose, state 1 when D_00810845 bit 5 is set else 4 (or 2 when 001BA1C0(self, 0x10) is set). **4** the idle sway; the player on polygon 1 at y >= 55 starts a 240-frame shake with random dust (0x8000001F); bit 5 moves it to 1. **1** sub 0: the player on polygon 2 (y >= 55, D_008106B9 == 0, D_008102B5 < 2 or 29..34) starts script 0x827180, places the player at (-270, 55, -583) facing +-pi/2, sound 0x8CC, D_00810768 = 1; sub 1: the shake (six 18-frame swings, dust, the player's y carried), the lift (30 frames), then the fall of 50 frames with the fixed dust / debris spawns at -30, -25, -20, -14 and -3, the pose reset, 001AF800, 001CB5B0(+9), 001B0FD0 and state 2 / sub 0. **2** D_00810768 = 0xFF and six 0x80000015 spawns, then 001BA1F0 while +0x2D0 is set. **3 / other** 001AFC10. |
| 0x8242C0 (self, st) | a script op09 callback | st +4: 0 D_008106C5 = 1, +1, returns 0; 1 with D_00810845 bit 5: 001FABB0 and 001FB0B0(0), returns 1; others 0. |
| 001CE860 (a0, a1, points, colour, n, tag, width, sp) | 0021A500 (the strip nodes 0x8000003B) | one GIF packet of 2 n vertices through 001CB5F0: each point projected (cull matrix 001CD370(0), view matrix 0x70003AC0, as 001CD940 with w - 3.0), the ADC bit 0x8000 when the cull clip test fails, the colour as integers, two vertices per point widened by the screen-space perpendicular of half width `width` (1 / its length at 0x7000368C, through the matrix 0x70003400 and 16 / w). Then 001CB900. |
| 0021A500 | the strip nodes | reused (SECURITY_GUN.md). |
| 001EBE10 (a0, a1) | the beam's collapse (a06_00 f296) | one 001CFB50 / 001CFBE0 pair (last float 3.0, table 0x2564C0, mode 1). |
| 002072C0 (self) | the keypad placement [6] (0x824340) | +3 == 0: the page record (001AFF10) gets +0x10 = 00207350, +0x20 = self, +3 + 1, 001AED80(0); +3 == 1: 00207D00(1, 3), 001B0000. |
| 00207350 (page, sp) | the page record | the keypad page (see the file comment): init, the dialog through the message block 0x2821B0 (modes / lines 0, 2, 4, 6), the cursor over 11 cells (pad 0x1000 / 0x4000 rows, 0x2000 / 0x8000 columns, 0x40 type / enter, 0x20 delete, 0x810 cancel), the compare with the slot's string (00123020): slot 0 right sets **D_00810845 bit 5** (the lock bit of AREA04's door [45]), the timers, the close (D_002821B4 = 2, 001AEDB0, D_008106C5 = 0xFF, 001AFF90). |
| 002079F0 (page, tex) | 00207350 | the page frame (five sprites) and the cursor sprite at D_00264FE0[cell]. |
| 00207BB0 (page, tex, sp) | 00207350 | the typed characters (001CC1E0, 00207D00) and eight star sprites. |
| 002072A0 | 00207350 | sound 0x8CD (the cursor move). |
| 00123020 (a, b) | 00207350 | the library string compare (word-, doubleword- and quadword-wise when both are aligned). |
| 00207CA0 / 00207CD0 (tex) | 00207350 sub-states 4 / 5 | one sprite each (tex +0x78 / +0x80). |
| 001FD0E0, 001FDDB0, 001AFF90 | the message presenters, the page's close | reused. |
| 0021AE90 (self) | a debris node (a06_05 f257) | 16 random (a, b) bone pairs of owner +0x24; strips 0x8000003B between the bones, two per frame on counts 0, 4, 8, 12, then state 3 at 16 (or when the owner is gone, +4 == 3); 2 / 3 001AFC10. |
| 001A8F40 (a, b) | the contact pass 001A9000 (class 0xD type 5 x class 4; a06_s1 f65) | \|b +0xB0 - a +0xB0\|^2 against (3 + *(a +0x30))^2: inside, b +0x36 = 0x2014 and the halfword 0x70003B88 = 0. |
| 00169250 (player) | 0015B130, player +5 state (the overhead bar's grab, a06_s1) | 0 / 1 turn toward +0x218 (pi / 8 per frame); on arrival clip 0x70, 8 frames, the rise step, the two bar ends (0019F680 of +0x30C), the snap point along the bar on its 4.5 grid (odd / even count); 2 clip 0xBA; 3 the 8-frame move and sound 0x123; 4 +5 = 0x10 (the hang, 00169730) with 001885B0's clip. |
| 0019F680 (out, rec, index) | 00169250 | the 12-byte row of the vertex table 0x700031FC named by the index list of rec; returns rec +0x18 (or 0 past the count). |
| 001885B0 (player) | 00169250 | the halfword D_002754CC[+0x235 & 1]. |
| 00169730 and its helpers, 00181D70 | the hang | reused. |

## 3. Verification

`python3 tools/test_area06_port_reference.py` (port root, macOS arm64 or
Linux; no make target: the Makefile belongs to another chain while this
side track runs). It compiles the four sources into
`build/area06/port/area06_port.dylib` with `-std=c11 -Wall -Wextra -Werror
-Wpedantic -ffp-contract=off`. At most 4 worker processes (EM_TEST_JOBS
overrides). `EM_AREA06_PORT_ONLY=<label prefix>` runs a subset (no
coverage, fail-stop, contract or reuse checks); the reuse checks of section
3.4 run only with `EM_TEST_FULL=1` (the default run keeps one smoke sample of
them); `EM_AREA06_PORT_MISSING=1`
lists unexecuted words; `EM_AREA06_PORT_SOURCE=<dir>` tests other copies of
the sources (the mutation sweep).

### 3.1 Oracle and harness

The AREA22 harness (docs/AREA22_PORT.md section 3, itself AREA04's),
reused, with the AREA04 harness's +0x4C callback, and these additions:

- **Images.** 14 recorded RAM images: the a22_02 end (the upper-floor
  arrival) and the ends of a01u_00..a01u_02, a01u_s0, a01u_s1 and
  a06_00..a06_05, a06_s0, a06_s1. AREA06 (id 6) is resident in a01u_02,
  a06_00..05 and a06_s1; AREA01 (id 2) in a22_02, a01u_00, a01u_01, a01u_s0
  and a06_s0; a01u_s1 ends in AREA22. Before any case the test asserts that
  each image's overlay text equals the user's `extract/OVERLAY/AREA06.BIN`
  or `AREA01.BIN` (size from the header), that the boot text equals the
  pinned ELF and that the two boot jump tables (001944B0's at 0x26DA50,
  00207350's at 0x273530, 9 words each) equal the ELF; their loads are the
  only accesses left out of the comparison (the switches encode them).
- **Stack.** Entries of kind S run the oracle with sp = STACK_TOP and give
  the native entry STACK_TOP; the window [STACK_TOP - 0x400, STACK_TOP) is
  compared memory, the frame bookkeeping excepted. A store's
  changed-or-not mark in the window is taken against the window as the
  logged stores leave it, because the nested translated functions' frame
  bookkeeping (saved registers) also lands there and is not made natively.
- **EE core additions** (subclass of the shared core, in the test file):
  psubw / psubb (00123020's quadword compare), VMAXbc / VMINIbc, VFTOI4,
  the clip test vclipw.xyz with the clip register (cfc2), as the other VU
  oracles model them.
- **Callees run as original code** (RUN): 0011DE90, 0011DF78, 0011E2A8,
  0011E748, 001026A0, 00102738, 00102760, 001028B8, 001028D0, 00102900,
  00102918, 00102948, 00102958, 001029C0, 00102BB0, 00122BB8, 001281C0,
  0018C4B0, 0018C6A0, 00191120, 001B1240, 001B12B0, 001B1470, 001B1EA0,
  001BA1A0, 001CD370, 001CD390, 001CFA60 (each first rehearsed with the
  argument registers its hook does not pass poisoned). Cases that need a
  given rand / point-in-polygon / step result stub those instead. Every
  other callee is stubbed with scripted results.
- **Compared, per case**: the calls and their arguments; memory at every
  call entry before the callee's writes are replayed; the memory accesses
  between calls one for one, in order, by address, size and
  changed-or-not; all memory after the last store (32 MiB, scratchpad,
  window); the return value; the store-log self-check; stops at unmapped
  or misaligned original accesses; every case again from a poisoned start
  image; coverage of every reachable original word of all 27 entries; the
  table's ctx at every call.
- **Fail-stop.** `fault_checks` (0021A440 on g[11]: NULL hook, failing
  hook, an unmapped address after three calls; 002072C0 for an unmapped
  address before any call; a latched fault; every entry with a latched
  fault, a NULL hook table, a NULL fault pointer and, for the seven
  entries with a result, a NULL result pointer) and `hook_contract_site`
  (on passing cases that together reach all 71 hooks, the +0x4C method and
  all 27 entries: every call failing, every memory access refused, each
  hook NULL / INT32_MIN / 1 / INT32_MAX, `bytes` NULL).

### 3.2 Cases

- **Capture** (156): on every image they apply to, 00219870 / 00219F50 /
  0021A180 / 0021A440 on g[11] (0x7A7690), 0x823580 on [9], 0x824560 on
  the beam (states 4, 1 and 2 as captured), 00169250, 001944B0, 001885B0,
  00176180 on the player and camera, 001EBE10, 001ECA20, 002072A0,
  001EA210.
- **Designed** (1,174 in the default run, 2,526 in the full run; 1,160 and
  2,512 before the 14 survivor cases of section 3.5 were added): every
  state and sub-state of every function with each callee result on both
  sides of its test (001B0FD0, 001B11E0, 0019AA80, 001B1630, 0019A570,
  001BA1C0, 001B1EA0, 0019AFE0, 001AFF10, 001EFEB0, 001C5570), scripted
  rand values for every remainder branch (negative, odd and extreme values
  of % 2, % 4, % 5, % 50, 16-bit samples), the float bounds of each
  compare (the beam's y >= 55, the phase wrap at pi, 21A180's fade at 0,
  00219870's +0x2C8 against 6 k, 001A8F40's contact radius, 001944B0's
  limits -46.8 / -30 / -20 and the clamps, 0x826950's 20 / 40 / 55 and
  its end); the keypad over all 11 cells and every pad combination, typed
  counts 0 / 7 / 8 / 9, the compare with the slot's string (copied from
  RAM at run time) right, wrong and short, every timer sub-state at 1 / 2
  / 0 / 0x8000; 00123020 on designed strings (no game text) at every
  alignment class, equal / differing at each byte of 8- and 16-byte
  chunks, bytes past the terminator, high-bit bytes; 001CE860 on designed
  strips (straight, vertical, far, behind the eye, repeated points,
  off-screen above / below, n 0..6, two widths); 0021AE90 on a designed
  owner with 40 bones; the signed counters and cursors at negative
  values, designed keypad cell positions, a tilted beam record, AREA06's
  sub-area 1, small beam positions and speeds (the float constants' last
  bit), strings whose terminator is alone in its 16-byte chunk (added
  after the mutation sweep, section 3.5); the 14 survivor cases
  (`survivor_cases`, section 3.5): the index-list entry -1, a high byte of
  b where a ends, typed count 0x80, (lim + 10) >> 2 at lim -13, negative
  rand words, goal -0.0 against a +0.0 turn result, w0 -3, the dust timer
  at 33, and a strip whose clip-space x, y and z equal |w| exactly.
- **Perturbed** (EM_TEST_FULL=1 only, 200): 40 seeded rounds of 00219870,
  0x824560, 0x823580, 00207350 and 001944B0 with random states, counters,
  floats and callee results.

### 3.3 Measured

Round-7 close, 2026-09-28, M1, host shared with other lanes (load average
about 8). Default run: 1,330 cases (156 capture + 1,174 designed), 2,214
runs (884 poisoned), 20,667 call entries compared, 9,791 helper register
rehearsals, coverage 4,619 / 4,619 reachable words, the fail-stop checks and
the hook contract (1,636 native runs on 59 cases: the greedy sites plus every
passing 0x826950 case) passing, the reuse smoke sample passing; 14.0 and
14.4 s CPU, 5.1 s wall with 4 workers (two runs; CPU includes the workers).
Moving the reuse checks to the full run took the default run from 21.3 s CPU
/ 15.6 s wall (the review's rerun) to this. It is still above the ~10 s
guideline: what remains is the cases themselves (each run as given and
poisoned, with every helper rehearsed), the coverage count and the hook
contract. `EM_TEST_FULL=1`: 2,882 cases (156 capture + 2,526 designed + 200
perturbed; 2,868 = 156 + 2,512 + 200 before the survivor cases), 4,991 runs,
43,264 call entries compared over all memory, 19,407 rehearsals, the hook
contract on 101 cases (2,907 runs; every passing case of 0x826950, 00176180,
001A8F40, 0x8242C0 and 002072C0), every reuse check at full size; 214.5 s
CPU, 70.3 s wall. All pass.

### 3.4 Reuse checks (the existing translations over the level-7 captures)

Each check runs the existing translation against the original with that
module's own harness (imported; the libraries are built into
`build/area06/port/reuse`). The four checks run serially and re-check
translations that have their own tests, so since the round-7 close they run
only with `EM_TEST_FULL=1`, at the full sizes below. The default run keeps
one smoke sample: 001AFF90 on four records (0, 6, 12, 18) of the a06_02
pool.

- **0021A500** (`em_gun_rest_0021A500`, the harness of
  `tools/test_security_gun_rest_reference.py`, `compare_run`): the live
  strip nodes of the AREA06 images as captured (all at +4 = 1, +5 = 1),
  each ticked until it frees itself or 40 ticks; the calls, their
  arguments and every written byte compared at each callee entry and at
  the end: all 27 live nodes (455 ticks). Their harness starts the
  scratchpad zeroed.
- **00169730 and its helpers, 00181D70** (`em_player_closure1019_*`, the
  harness of `tools/test_player_closure_10_12_19_reference.py`,
  `run_batch`): its seeded state cases over the a06_s1 and a06_00 RAM, the
  player record from each capture (their test uses the AREA11 RAM): 3,000
  cases per image.
- **001FD0E0 / 001FDDB0** (`em_cs_001FD0E0` with `em_mpr_001FDDB0`,
  `tools/test_message_presenter_rest_reference.py` section C): the real
  cue lines 0, 2, 4 and 6 of the message block 0x2821B0 (the keypad page's
  dialog lines) frame by frame over the a06_02 RAM, each to its end.
- **001AFF90** (`em_status_scene_free_001AFF90`, the PoolRig of
  `tools/test_status_scene_reference.py`): the status pool 0x28B020 as
  captured at the a06_02 and a06_03 ends, each of the 24 records freed
  through itself (48 frees; the default run's smoke sample is 4 of them).

### 3.5 Mutation sweep

One bounded sweep (`build/area06/port/sweep.py`, not committed; results
`sweep_results.json`, re-run `resweep_results.json`). 6,440 single-edit
mutants of the four sources were generated (every float / address macro
with its last bit flipped or + 4, every integer literal +-1, operator swaps
`==` / `!=`, `<` / `<=`, `>` / `>=`, `&&` / `||`, `+` / `-`, `&` / `|`,
`>>` / `<<`, `!` removed, EE add / sub, mul / div, lt / le, access widths
and signedness 8 / 16 / 32, every store, call and `if (hook) return` line
dropped, and every pair of adjacent simple statements swapped); a seeded
sample of 900 ran, each first on its entry's cases (EM_AREA06_PORT_ONLY),
then, surviving that, on the whole default run.

- 37 do not compile; of the 863 that run, 785 were killed (767 by their
  entry's cases, 18 by the whole run: coverage, fail-stop, hook contract).
- The 78 survivors led to the cases added in section 3.2 and one contract
  change (every passing 0x826950 case is a contract site, because that
  entry returns from its own body): re-run against them, 11 more are
  killed (the last bit of the 0.04 and 35.0 constants, the 16-byte zero
  test starting at byte 1, three signed-versus-unsigned counter reads,
  the keypad cell table read unsigned, D_00810845 tested with 0x21, the
  lift's add / sub, the sub-area row index, an entry returning -2).
- **67 survive, 66 equivalent**: 27 change the return value of a fault
  path inside a helper (the entries map any failure to -1 and write no
  result); 17 change an initializer that a hook result always overwrites,
  an array size or a C array-parameter size, a truth value used only as a
  truth value, the accumulator lanes a dest-masked VU0 op never reads, or
  the second operand of the fog lane's VMAXbc (0 or the smallest
  denormal, both 0 after VFTOI4); 15 swap two statements with no memory
  access between them or only a pure computation / declaration; 5 touch
  001CE860's clip branch for lanes with exponent 255 (unreachable: every
  VU0 result is saturated below it) or the sign bit of a clip flag (only
  its being nonzero is used); 3 more are a 9-byte store later overwritten
  by the tag word, the `t & 0xE` test after `t & 7`, and a loop bound 15
  with step 4 (same iterations). The one survivor this sweep left unproven,
  001CE860's clip compare with w's lowest mantissa bit cleared, is now
  killed by the survivor strip case below.
- The sweep covers these 900 mutants only.

**Round-7 review sweep and the survivor cases.** The round-7 review ran its
own bounded sweep (40 hand-picked semantic mutants; scratch
`build/r7review/A06T` in the decomp repo). It left 9 survivors that are not
equivalent, in value domains the designed cases did not reach: 0019F680's
row halfword read unsigned; 00123020 returning the signed difference at a's
terminator; 00207350's typed count compared signed; 00219870's
(lim + 10) >> 2 as a division by 4; 0021AE90's bone index from the unsigned
rand word; 00169250's arrival test as bitwise equality; 0x823580's
w0 % 3 == 0 taken unsigned; 0x824560's dust timer tested with & 0x1F; and
001CE860's clip compare > as >=. `survivor_cases` adds 14 designed cases
for them (the review's probes P1-P3, P5-P8 and P10, and one 001CE860 strip).
The strip patches the cull matrix 001CD370(0) returns (the render context +
0x2240; 001CD370 still runs as original) to the identity with row 3 =
(0, 0, 0, 1.0000001), so the clip-space x, y and z of its points equal |w|
= 0x3F800001 exactly, with w's lowest mantissa bit set. Each case runs
against the original and is identical on the lane's sources. At the round-7
close each mutant was applied to a scratch copy of the sources
(`EM_AREA06_PORT_SOURCE`) and run on its survivor cases: all 9 are killed,
and the strip case also kills the lane's own open survivor above (w with
its low bit cleared). No survivor of either sweep is left unproven; the two
equivalent ones of the review sweep are the 0x824560 dust gate with a
logical shift (the gate only tests x != 0) and 001CE860's unsigned-to-float
sticky bit (the conversion truncates, so bit 0 of a value of 2^30 or more
never changes the result).

## Binding

Nothing calls these entries. To run them live a host must:

- supply `bytes` over the scene's original-byte storage (the actor pool,
  the player and camera blocks, the eye / target, the status pool
  0x28B020, the message block 0x2821B0, the scratchpad, the placement
  tables D_0024D7C0, the stack frame below `sp`);
- run 0x823580 / 0x824560 / 00219870 / 0021AE90 as the node behaviours
  (+0x10) of their nodes and 00207350 as the page record's; bind the
  +0x4C method `w_callback` to the owner draw;
- call 0x8242C0 and 0x826950 from the script host's op09 records (AREA06's
  and AREA01's scripts 0x82B590);
- call 001944B0 from the camera dispatch (`em_camera_area11_specials.h`
  w_001944B0), 00169250 from the player state table (+5 = the bar grab),
  00176180 from the hull shove (`em_player_floor.h`), 001A8F40 from the
  contact pass (`em_coll_list_passes.h` w_001A8F40), 001CE860 from the
  strip nodes (`em_security_gun_rest.h` w_001CE860), 001885B0 from the
  closure's clip row (`em_player_closure_live.c` x_001885B0);
- bind each hook to a verified port translation or a fail-stop stand-in.
  The stubbed callees (001B0FD0, 001C6380, 001C5570, 001B11E0, 0019AA80,
  001FBD50, 001B1190, 001B1630, 001B1B70, 001EFD20, 001EFEB0, 001AFC10,
  0019A570, 001A2370, 001CFBE0, 001CFB50, 001CCF70, 001CB5F0, 001CB900,
  00182F90, 001FB9F0, 00194240, 001749A0, 001D2DE0, 001AFF10, 001AED80,
  00207D00, 001B0000, 0020CD60, 001AEDB0, 001AFF90, 001CC1E0, 00207E40,
  001F02C0, 0019C6F0, 001FABB0, 001FB0B0, 001BA1C0, 001BA1F0, 001AF800,
  001CB5B0, 0018D7B0, 0019AFE0) are not verified here; the helpers run as
  original code need port translations paired by address.

## Known gaps

- **Live evidence.** The captures are end-of-beat states: the keypad page,
  the bar grab (00169250 states 0..3), the beam's shake / lift / fall,
  0021AE90's debris node, 001CE860's strips and 0x826950's walk are not
  live in any captured image; their cases are designed (on the captured
  RAM, with the state bytes patched). 00219870 and 0x823580 are captured
  in state 1, the beam in states 4, 1 / 0 and 2 / 1.
- **Stubbed callees' inputs.** Only the RUN helpers are proven not to read
  a register their hook does not pass; callee results are scripted beyond
  their real ranges.
- **What the harness cannot see** (as AREA04): whether an access that
  changed nothing is a load or a store of the same value; hardware
  behaviour past a stop; the frame bookkeeping.
- **Float proofs rest on the EE model** (`em_ee_float.h`) and, for
  001CE860, on its measured VU0 forms; a clip lane with exponent 255 is not
  measured (the translation faults 6, the test's core refuses the case).
- **Reuse checks** use the other harnesses' own models (the gun-rest
  harness's zeroed scratchpad, the closure harness's seeded records, the
  status PoolRig's slot stack); the message presenters are checked on the
  four keypad cue lines only.
