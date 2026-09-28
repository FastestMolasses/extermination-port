# AREA01 lane UI

Lane UI of the level-2 (AREA01) side track, 2026-09-26. The census delta
(`../Extermination/build/s87/census/a01_delta.json`) lists 17 new boot
functions in the subsystems ui_screens, ui_credits and draw2d (18,788
bytes), all side-beat-only. The subsystem names are labels; nothing below
relies on them.

| addr | bytes | decomp source | census beats (first frame) |
|---|---:|---|---|
| 00207D90 | 172 | byte-matched C | a01_s2 (f280) |
| 00208040 | 352 | byte-matched C | a01_s2 (f280) |
| 0020F950 | 1752 | NEARMISS | a01_s2 (f279) |
| 00210030 | 392 | NEARMISS | a01_s2 (f280) |
| 002101C0 | 2108 | NEARMISS | a01_s2 (f280) |
| 00210A00 | 508 | byte-matched C | a01_s2 (f280) |
| 00210C00 | 812 | byte-matched C | a01_s2 (f280) |
| 00210F30 | 780 | NEARMISS | a01_s2 (f280) |
| 00211400 | 976 | NEARMISS | a01_s2 (f280) |
| 002131B0 | 776 | byte-matched C | a01_s1 (f330), a01_s2 |
| 002134C0 | 1332 | NEARMISS | a01_s1 (f330), a01_s2 |
| 00213F30 | 240 | byte-matched C | a01_s1 (f329), a01_s2 |
| 00214020 | 1352 | NEARMISS | a01_s1 (f329), a01_s2 |
| 0022B700 | 160 | byte-matched C | a01_s3 (f209) |
| 0022B7A0 | 972 | byte-matched C | a01_s3 (f209) |
| 0022BB70 | 68 | byte-matched C | a01_s3 (f209) |
| 0022BBC0 | 6036 | NEARMISS | a01_s3 (f209) |

All 17 are translated here: `src/game/em_area01_ui.h`,
`em_area01_ui_internal.h`, `em_area01_ui_pages.c` (the first 13) and
`em_area01_ui_effect.c` (the last 4). The test is
`tools/test_area01_ui_reference.py`. Nothing is wired and no tracked file
was edited.

**Roles, from the code and the captures.** The first 13 run as the
status screen's page handlers (0020CDC0 phase 3 calls them with the status
block t = D_00810130; FINDINGS "STATUS SUB-PAGES" names 0020F950 page 1 and
00214020 page 3, and the game's own text calls them MAP and DATABASE:
a claim of FINDINGS, not re-checked here). In the captures, t at the end
of a01_s1 holds page 3 closed on record 0x48 with the category list
0x48, 0x6D, ..., the route's DATA BASE pickup (item 0x48); a01_s2 ends
with page 3 on record 0x20 and map 0 owned (0x810CB8 = 1). 0022BBC0 is a
pool callback: in the a01_s3 end image the node 0x7B0F50 runs it, with
seq[0xD] = 9, seq +0x24 = the player block 0x8102B0 (21 bones) and its
5 bone slots holding a 65-slot ring. The census label "ui_credits" is
therefore wrong for this route: the one capture that runs it is the fire
contact, where it drives the player's burn particles. (The decomp comment
calls it the staff-roll director; seq[0xD] selects variants 0..9, of which
the route showed 9 only.)

**Bound in AREA11 (chain C8b):** 00214020 (DATABASE) with 002131B0,
002134C0 and 00213F30 runs live in the first level through
`em_status_pages_live` (STATUS_PAGES.md section 7), with its callees
dispatched there (00207D90 is its SCISSOR_1 packet's leaf in
`em_page_draw`); these files are in the main build. Since chain C8b's fix
round 0020F950 (MAP) with 002101C0 (its nodes, run from the UI pool's walk),
00210030, 00210A00, 00210C00, 00210F30 and 00211400 run live there too.
With `EmArea01Ui.leaf_calls` set (the live binding only), 0020F950 and
00210F30 reach 00207D90 and 00208040 through `call` (the argument registers
the original loads) instead of running their translations, so the scissor
and the marker triangle land in `em_page_draw`'s stream; with it 0 (this
lane's test, the status lane's) they run as before. The model bank the
nodes bind is the disc's (STATUS_PAGES.md section 7, "MAP"), not this
lane's stand-in banks.

**Overlap with existing port files** (grep of src/ for each address; no
translation of any of the 17 exists):
- `src/game/em_area11_interaction_host.c` and `src/game/em_hud.c` name
  0020F950 / 00214020 only in the page-core fault report and a comment
  (the AREA11 host faults when a take selects those pages,
  FIRST_LEVEL_AUDIT.md H7 / H9). These translations are what that fault
  waits for; binding them is the lead's.
- `src/game/em_player_closure_live.c` (and EFFECT_MANAGER.md) note that
  0022BBC0 has no AREA11 binding row.
- `src/game/em_area01_render_gs.h` lists 0022BBC0 as a caller of 001CD070
  and 001CD2B0, which the render lane translated. Here they stay callees
  through `call` (so the oracle runs them as original code); the lead can
  bind them to `em_area01_render_001CD070` / `_001CD2B0`.

## 1. Modules and what each function does

Contract (both files). Every original byte is reached by its original
address through the views of `EmArea01RenderCore`
(`em_area01_render_mem.h`, reused): EE RAM, the scratchpad at 0x70000000
and, for the two stack buffers the routines hand to callees, the EE stack.
Each routine carves its frame below `EmArea01Ui.sp` with the original's
frame size, so a stack address a callee receives (002134C0's number
buffer at sp + 0x3C, 0022BBC0's 001CFB50 block at sp + 0xA0) is the
original's. Every callee outside the lane goes through one dispatcher,
`call(ctx, target, sp, a[], na, f[], nf, &v0, &f0)`: the integer argument
registers a0..a3, t0..t2 as full 64-bit images (a 32-bit int passed
sign-extended, as the EE holds it; 00207E40's texture word and 0020A7A0's
argument are 64-bit), the float argument registers f12.. as binary32 bits.
00102948 and 00102958 (quadword copies) run inline. 002101C0's call
through the node's +0x4C passes the loaded pointer as the target.

Fail-stop, the math lane's sticky form: the first fault latches (code 1
`call` NULL, 2 a callee returned < 0, 4 an address no view covers, 6 see
section 2); after it loads read 0, stores are dropped, no callee is
called, and every entry returns -1. Arithmetic: every COP1 operation
through `em_ee_float.h` on bit patterns.

### Status pages (`em_area01_ui_pages.c`)

- **0020F950(t)**, by t[3]:
  - 0: 22 pool nodes from 001AFF10, each with callback 002101C0, p[3] =
    map 0..10, p[0xD] = 0 for the first eleven and 1 for the rest; t[3] += 1;
    D_002821B0 = 4, D_002821B4 = 0, D_00282240 = 6; t[0x12] = 0xFF when no
    map byte (0x810CB8..0x810CC2) is set, else D_008106CD & 0xF, stepped
    down (0 wraps to 10) to a set map when its byte is clear; t[0x14] =
    (D_008106CD & 0x30) >> 4; +0x28 = +0x2C = 0; a pending request
    D_008106B0 is cleared and selects map D_008106B1 with t[0x13] = 1,
    +0x24 = the float at 0x2657C0 + map * 20 and t[3] = 2; else t[0x13] =
    0, +0x24 = 0.10666667.
  - 1 (list): 00210A00(0), 00210C00(0), 00207D00(1, 3), 001B0000 (the UI
    pool, which ticks the 22 nodes), 00211400(t, 0), one 00207E40 sprite.
    Triangle edge (D_00810E74 & 0x20): 0020CD60, t[0x10] = 0x63,
    D_002821B4 = 0. Else, unless t[0x12] == 0xFF: with t[0x13] == 0 the
    pad repeat D_00810E78 & 0x2000 / 0x8000 moves the cursor up / down to
    the next set map (0020CDA0 when it finds one; at most 11 steps); Cross
    (0x40) on a set map: 0020CD40, t[0x13] = 1, +0x24 from 0x2657C0, the
    marker offset +0x28 / +0x2C (0 in areas 0x12 / 0x15 or off the current
    map; else from the camera block 0x810350 / 0x810358 and the floor row
    D_00265890[map] + t[0x14] * 12, turned through 001029C0 / 00102BB0 /
    001026A0 on 0x70003400 / 0x70003600; +0x28 = the turned z
    (0x70003608) negated, +0x2C = the turned x (0x70003600), both scaled
    by 2 * 0.10666667), t[3] = 2; Cross on a clear map: 0020CD80. Then
    D_002821B4 = 1, D_002821B8 = t[0x12].
  - 2 (map view): 00210A00(1), blend 3, 00207D90(1, 0x12, 9, 0x1EF, 0x96),
    001B0000, 00210F30(t) when the map is the current one, 00211400(t, 1),
    00207D90(1, 0, 0, 0x200, 0xE0), one sprite, 00210C00(1); Triangle:
    0020CD60, t[0x13] = 0, +0x2C = +0x28 = 0, t[3] = 1. D_002821B4 = 1,
    D_002821B8 = t[0x12].
  - Other values: nothing.
- **002101C0(p)** (node callback). The bytes the decomp calls D_00810142,
  D_00810143, D_00810144 and the floats D_00810154/158/15C, D_00810170/
  174/178 are t[0x12], t[0x13], t[0x14] and t+0x24 .. t+0x48 of the
  status block (the node reads its page's state by absolute address; the
  test shows 0020F950 mode 0 writing D_00810143). By p[4]: 0 sets the model
  up when the map's byte is set (001CA5E0(p, 001C6120(D_0028A570,
  the word at 0x2658C0 + map * 8 + 4 * p[0xD]), 7), p[0xC] = 001C6150(p+0x44
  word); above the halfword cap D_00275BCC p[4] = 3 and return; else one
  001AF7C0 slot per bone at p+0x110, p[9] = p[0xC], 001CB5B0(p[0xC]),
  001C62C0(p)), then (either way) p[2] |= 0x40, colour, p[4] = 1, scales
  0.10666667, +0xA0 = -(0x265600 + map * 8), +0xA8 = (0x265604 + map * 8),
  and on into 1. 1 sets the colour by current map (D_008106CD & 0xF) and
  selected map (t[0x12]) (p[0xD] == 1 nodes return unless selected); with
  t[0x13] == 0 it resets the scale and, on a set map, runs 00210030(p, 0),
  001C6380(p) and the +0x4C call; with t[0x13] != 0, on the selected set
  map, p[0xD] == 0 nodes step the zoom t+0x24 (pad 2 / 8 by 0.1, clamped
  to 2 and 10) and the pans t+0x28 (0x2000 / 0x8000 by 2) and t+0x2C
  (0x1000 / 0x4000 by 2) against the map's limits at 0x2657B0 + map * 20,
  then scale 0.10666667 * zoom, 00210030(p, t[0x13]), 001C6380, +0x4C.
  Last, a p[0xD] == 0 node of the current map in zoom mode (area byte not
  0x12 / 0x15) recomputes the view centre t+0x40 / t+0x48 from the camera
  block and the floor row, turned by 001029C0 / 00102BB0 / 001026A0 on
  0x700036A0 / 0x700038A0, then offset by half the zoom times the pans over
  0.10666667 * zoom and scaled by 0.10666667 * zoom (x) and 0.08533333 *
  zoom (z). p[4] = 2, 3 or above 3: 001AFF90(p).
- **00210030(p, a1)**: a = (p+0xA0 - 85.0014) * p+0x60 and b = p+0xA8 *
  p+0x60 for a1 == 0; else t = zoom / 2, b = 0 + t+0x28 * t, a = -60 +
  t+0x2C * t. p+0xB0..B8 = the camera words 0x81061C/62C/63C, plus
  0x810618/628/638 * (render context)+0x2468 (the context pointer read
  again for each), plus 0x810610/620/630 * b, plus 0x810614/624/634 * a,
  each field read back before its add.
- **00210A00(a0)** (byte-matched): 0020A7A0(0x20043C259D422050), blend 0,
  five sprites, a sixth when a0 != 0, blend 3, one sprite.
- **00210C00(a0)** (byte-matched): blend 3; a left / right pair chosen by
  D_00810E70 bits 0x2000 then 0x8000; with a0 == 1 an up / down pair by
  0x1000 then 0x4000 (the halfword read again).
- **00210F30(t)**: nothing in areas 0x12 / 0x15; else the marker triangle
  of the current position: (x, z) = t+0x40 / t+0x48; 001029C0 on
  0x700036A0 and 0x700036E0, whose diagonal becomes 0.8, 1.0, 0.5 and
  +0x3C = 0; 0x70003A20 = (D_00810374 - the floor row's angle) - pi/2,
  wrapped by 001B1470; 001026D0 / 00102BB0 compose 0x700036A0;
  0x700038A0 = (256 + z, 0, 164 - x, 1) through 001031E0(0x700036D0, it);
  the corners 0x26A990 / 9A0 / 9B0 copied to 0x700038A0 / B0 / C0 and turned
  by 001026A0; each scaled to 16 * (1792 + x), 16 * (1936 + z / 2); blend
  3; 00208040(1, the three corners, 0x802040A0).
- **00211400(t, a1)**: nothing unless the scratchpad counter 0x70003B64 & 3;
  blend 0; a1 == 0: 00211240(k) for each k = 0..4 whose byte pair holds
  (0x810C8D / 0x81076D, 0x810C8E / 0x810770, 0x810784 == 1, 0x81077F ==
  0xFF with 0x810789 != 0xFF, 0x810782 == 0xFF with 0x81078C == 0); a1 != 0:
  for t[0x12] = 5, 6, 0, 3, 4 and its pair holding, 0x700038A0..A8 = the
  triple at 0x265940 + 12 * k, 002117D0(t, 0x700038A0, t[0x12], 2 / 2 / 0 /
  0 / 3), 00211310(0x700038A0).
- **00214020(t)**, by t[3]:
  - 0: t[0x17] = t[0x19] = t[0x1A] = 0, halfword +0x1E = 0, D_002821B0 =
    4, D_002821B4 = 0, D_00282240 = 0x64. With a pending request: t[0x1B]
    = D_008106B1; t[0x12] = its category (< 0x20, 0x32, 0x48, 0x5D, else:
    0..4); 00213F30(t); the first list slot holding the id sets t[0x19] =
    slot - 4 (plus the list length t[0x18] when that byte is negative),
    t[0x17] = 4 and D_00282240 = 0x64 again; D_00282244 = 0, D_002821B8 =
    the id, +0x1C = 0, t[3] = 4, D_008106B0 = 0. Without: t[3] += 1,
    t[0x12] = 0, 00213F30(t), and on into 1.
  - 1: Triangle: 0020CD60, t[0x10] = 0x63, D_002821B4 = 0. Else
    0020A7A0(0x200450A59D422200); repeat 0x2000 / 0x8000 steps the category
    (wrapping 0..4) with 001FB9F0(0xF, 0x1000, 0x1000, 0x1000) and
    00213F30(t); 002131B0(t, 0), 002134C0(t, 0); when 00213A00(t, 0) is
    nonzero +0x1C = 0, t[3] += 1, 00213C50(t, t[0x1A]); else Cross: t[0x1B]
    = t[0x50 + 0020BEF0(t)], and an id (not 0x6D) gives t[3] = 4 (stored
    before the cue), 0020CD40, D_00282244 = 0, D_002821B8 = the id, +0x1C
    = 0; the blank 0x6D gives 0020CD80, t[3] = 3, t[4] = 8.
  - 2: 0020A7A0, 002131B0(t, 0), 002134C0(t, 0); t[3] -= 1 when
    00213CC0(t) != 0.
  - 3: the same three, 00213A00(t, 0x400); t[4] -= 1; at 0 D_008106B0 = 0,
    t[3] = 1.
  - 4 (the page): 0020A7A0, 002131B0(t, 1), 002134C0(t, 1), D_002821B4 =
    1; with 2 or more pages (D_002659C0[t[0x1B]], a word) repeat 0x2000
    turns forward (0020CDA0 then +0x1C += 1; at the last page +0x1C =
    that count's low halfword - 1, without a cue) and 0x8000 back (store,
    then 0020CDA0; not below 0); D_00282244 = +0x1C; Triangle or Cross
    (0x60): D_002821B4 = 0, 0020CD40, t[3] = 1.
  - Other values: nothing.
- **002131B0(p, a1)** (byte-matched): blend 0, four frame sprites (two
  sets by a1), blend 3, two sprites, then one sprite (a1 != 0) or
  001FCF30(p[0x12], 0x64, 0x2F).
- **002134C0(p, a1)**: blend 3. a1 == 0: arrow pairs by D_00810E70 (0x2000
  / 0x8000, then 0x1000 / 0x4000, read again). a1 != 0: the numbers +0x1C
  + 1 and D_002659C0[p[0x1B]] through 001C5FB0(n, 2, 1), 00123168(sp +
  0x3C, it) and 001CBA50(1, 0x870 / 0x898, 0x7AC, 0x10, 0x10, sp + 0x3C,
  0x275870); with 2 or more pages a next arrow (+0x1C < count - 1) and a
  previous arrow (+0x1C > 0), each texture chosen by 0x70003B64 & 4.
- **00213F30(p, lo, hi)** (byte-matched): p[0x18] = 0; category p[0x12]
  0..4 picks [0, 0x20), [0x20, 0x32), [0x32, 0x48), [0x48, 0x5D), [0x5D,
  0x6D) (other values keep lo / hi, the a3 / t0 registers); each id appends
  itself (owned byte 0x810CC3 + id set) or 0x6D at p + 0x50 + p[0x18]++;
  p[0x19] = 0.
- **00207D90(slot, b0, b2, b1, b3)** (byte-matched): the slot record
  (render context + slot * 4) +0x10 cursor, read again before each of the
  header stores (+3 = 0x10, +4 = 0, halfword +0 = 3), cursor += 0x40; the
  packet: quadword +0x10 = 0 (low address bits ignored), +0x1C =
  0x50000002, +0x20 = 0x1000000000008001, +0x28 = 0xE, +0x30 = b0 | b1 <<
  16 | b2 << 32 | b3 << 48 (lanes sign-extended), +0x38 = 0x40.
- **00208040(slot, a, b, c, rgba)** (byte-matched): header halfword 5,
  cursor += 0x60; +0x1C = 0x50000004, +0x20 = 0x5400000000008001, +0x28 =
  0x44410, +0x30 = 0x143, +0x38 = rgba's low word zero-extended; then for
  each of a, b, c the doubleword sx32(float_to_int(x) | float_to_int(z) <<
  16) | 0xFFFFFF << 32 (z converted first, x read after that call); +0x58
  = 0.

### Effect driver (`em_area01_ui_effect.c`)

- **0022BB70(i)** (byte-matched): the word at *D_00275B40 + (i / 13) * 4
  plus (i % 13) * 16 (truncating division).
- **0022B700(p, n)** (byte-matched): p[0xC] = n; above the cap D_00275BCC
  p[4] = 3, result 0; else p+0x110[k] = 001AF780(p[0xC]) per bone,
  001CB5B0(p[0xC]), p[9] = p[0xC], p[4] = 1, result 1.
- **0022B7A0(e)** (byte-matched): the timeline table by e[0xD] and the
  record q = e+0x24 (q[2], q[3], q[0xD], D_008101E4), stored at e+0x280;
  the 8-byte records are counted through the first whose halfword +2 is 8
  and the count stored at e+0x288; result 1, or 0 with nothing stored for
  combinations without a table (the full map is in the source comment).
- **0022BBC0(seq)** (scn = seq+0x24, w = seq+0x1F0), by seq[4]:
  - 2, 3: 001AFC10(seq, seq[4]). Above 3: nothing.
  - 0: 0022B7A0(seq) == 0 sets seq[4] = 3. Else w[i] = w[i + 0x38] = 0 for
    each bone, 0022B700(seq, 5) (0 ends the call), the 65 ring slots' age
    +0xE = -1, the LCG w+0x94 = 00122BB8(), w+0x9C/A0/A4/B0/AC = 0,
    seq+0x80 = the quadword scn+0x80, and on into 1.
  - 1: scn[4] == 3 sets seq[4] = 3 and ends. Else the timeline: every
    record (w+0x98 of them at w+0x90) whose halfword +0 is the clock w+0xAC
    runs its op +2 (0..8; others none): 0 tint ramp to (1, 0.1, 1) over +6
    frames (seq[0xD] 0 only); 1 scroll count = +6, step = 0x1000 / +6 + 4
    (a zero divisor faults, code 6); 2 w[+4] = 1 and w[+4 + 0x38] = +6's
    low byte; 3 bone +4's halfwords 0x88..0x8C = 0 (seq[0xD] 0); 4 scn[4] =
    3 (seq[0xD] 0); 5 fade ramp of scn+0x80..8C to (0.2, 0.8, 0.2, 1) for
    seq[0xD] 0 and (0.1, 0.1, 0.1, 1) for 1 and 3; 6 the ramp back to
    seq+0x80..8C (seq[0xD] 9, 3, 1); 7 001CA6E0(scn, D_0028A490[+6])
    (seq[0xD] 1); 8 seq[4] = 3. The loop continues after 4 and 8. The
    ramps then run (w+0x9C frames onto scn+0x80..8C, w+0xA0 onto
    scn+0x60..68, w+0xA4 subtracting halfword w+0xA8 from every bone's
    +0x8A, floored at 0).
  - The emitters, per bone i with w[i] != 0: the offsets 0x700038B0..CC,
    the scale 0x70003A20 and the period by seq[0xD]; when w[i] % period ==
    1 the bone position (bone +0..8, w = 1) scaled (00103230), offset
    (001028B8 by 0x700038C0) and turned (001026A0 by bone +0x90) plus
    0x700038B0.. lands in ring slot w+0xB0 (kind +0xC = w[i + 0x38], age
    +0xE = 0) and w+0xB0 advances modulo 0x41; w[i] += 1; then seq[0xD] 0
    / 6 / 7 / 8 fade the bone's +0x88 by 0x66 (below 0: +0x88..8C and w[i]
    cleared), 1 / 2..5 / 9 wrap w[i] at 0x51 / 0x79 / 0x3D, and 9 also
    draws the bone's burst: kind w[i + 0x38] 0..5 sets scale 0x70003A20,
    size 0x70003A24 and the table pair; 0x70003A28 = 2 * (w[i] / 60); the
    bone's 4 x 4 at +0x90 copied to 0x700036A0, 00102760 on its three rows,
    00103230 by the scale; h = 001CCF70(0x700036D0); 001F0190(-145, 450);
    kind 5: three rounds n = 1..3 with the floats at 0x268910 / 918 / 9A0
    / 9A8 = 3n, 001CFB50(sp + 0xA0, 0, 0x700036A0, 0x70003A28, 0.123456n,
    1, 1e-6, size), 001CFBE0(h, 1, 0x268990 / 0x268900, sp + 0xA0, 1);
    other kinds: one 001CFB50 and 001CFBE0 with the tables 0x268480 +
    (burst + 1) * 0x90 and + burst * 0x90; 001F0290.
  - The ring: 0021B9A0(2, 1, 20), 0021B9A0(3, 1, 20); for each of the 65
    slots, a three-step LCG (x * 37 + 11) from w+0x94 gives three jitters
    ((x >> 16 & 0xFFFF) / 65535 + 1e-4 at 0x700038D0..D8) and 0x70003A24 =
    1e-4 * age; a slot whose 0x70003A24 is not negative gets scale 1 / 1.5
    / 2 (kinds 0, 3, 4 / 1 / 2; others keep 0x70003A28), 001029C0 and
    00103230 on 0x700036A0, its position at 0x700036D0 and, when
    001CD070(0x700036D0, 0x30) is on screen, the seq[0xD] draw (0: alpha =
    001CD2B0(15, 20, 320, 320) at 0x70003A2C and three 001CFB50 /
    001CFBE0 layers unless alpha is 0; 1, 2 / 3, 4, 5, 6..8: one or two
    layers from their tables) and age += 0xC8 (0xFA for 6..8, 0x3A98 for 9
    and above); an age of 0x3A99 or more becomes -1. 0021B9A0(1, 0, 0);
    the clock w+0xAC += 1.

## 2. Where the decomp's NEARMISS C differs from the instructions

The translations follow the instructions. For the lead's FINDINGS (no
decomp file was edited):

- **0022BBC0, period and burst.** The C initialises `period = 0` and
  `burst = 0` before the emitter loop. The instructions keep both in
  callee-saved registers (s1, s3) that are set only inside the seq[0xD]
  switch (period) and the burst-kind switch (burst); the loop does not
  initialise them. So with seq[0xD] >= 10 and a live bone the original
  divides by the caller's s1, and in variant 9 a first emitting bone whose
  kind w[i + 0x38] is outside 0..5 indexes the tables with the caller's
  s3. The translation faults (code 6) in both cases rather than invent a
  value; the fault group tests both. Neither arises on the route (the a01_s3
  node has seq[0xD] = 9 and kinds 0).
- **0022BBC0, 001029C0's argument.** The C passes (0x700036A0, seedB); the
  instructions set only a0 (the test logs the call with one argument).
- **0022BBC0, 0022B7A0's argument.** The C comment says 0022B7A0 "reads the
  state byte the caller left in a1"; the byte-matched 0022B7A0 takes e in
  a0, which the caller leaves as seq.
- **00214020 -> 00213F30.** The C calls 00213F30 with one argument. The
  byte-matched 00213F30 reads lo / hi from a3 / t0 when the category byte
  is outside 0..4. In state 0 the caller's a3 / t0 are 2 / 3 (set by its
  dispatch) but the category is always 0..4 there; in state 1 after
  001FB9F0 they are whatever that callee left, reachable only with a
  category byte 6..0x80 and a decrement. The translation faults (code 6)
  there; tested in the fault group.
- **002101C0 names.** The C's D_00810142/143/144 and D_00810154..178 are
  bytes and floats of the status block t (0x810130 + 0x12.. and + 0x24..),
  which 0020F950 writes (its mode 0 clears t[0x13] = D_00810143).
- **0020F950, the cursor search.** Faithful; note that "owned" and
  the D_00810CB8 gate read the same bytes (0x810700 + 0x5B8 = 0x810CB8).
  The search starts at the current-map nibble D_008106CD & 0xF, which can
  be 11..15, and then reads 0x810CB8 + 11..15 (0x810CC3.., past the eleven
  map bytes). With a nibble of 0..11 its 11 downward steps visit every
  map, so it cannot run out (it runs only when one of the eleven map
  bytes is set); with 12..15 it can: nibble 15 with only map 0 owned and
  0x810CC3..0x810CCA clear steps 14..4 and runs out (t[0x12] stays 4),
  a pinned case. A set byte past the map bytes can also stop it on a
  value 11..14.

## 3. Verification (original-instruction oracle)

`python3 tools/test_area01_ui_reference.py` (EM_TEST_FULL=1 for every case;
EM_TEST_JOBS=1 serial). It imports the render lane's machinery
(`test_area01_render_reference.py`: RouteEE with every COP1 / VU0 op from
`ee_float_model.py`, the EE access rules and TLB map of the AREA01
captures, the store journal, the per-process RAM buffers and the
dirty-page tracker) and builds under `build/area01/ui/`.

- **Oracle.** The original routine runs over a captured end-of-beat image
  (`route_a01/<beat>/{eeMemory,scratchpad}.bin`: a01_s1, a01_s2, a01_s3,
  a01_05); every callee outside the lane runs as ORIGINAL code, nested,
  and is logged with its stack pointer, every integer argument register it
  takes (64-bit) and every float argument register; the lane's indirect
  call is logged by its loaded target. The code bytes of the 17 routines
  and of every callee entry are checked against the pinned ELF on the four
  images, and the callee set of the 17 routines is asserted equal to the
  dispatcher table.
- **Native.** The translation runs over a second copy; each `call` runs
  the same original callee in a second interpreter sharing the native
  memory, with every register the call does not pass poisoned.
- **Entry check (every callee call).** Before the native side runs callee
  k, its logged entry must equal the oracle's and all 32 MiB of RAM and
  the 16 KiB scratchpad must equal the oracle's at the same call (the
  oracle's store journal replayed onto the start image; pages compared as
  the render lane does). **End check:** results, the call log, RAM and
  scratchpad identical.
- **Replay and memo (quick mode only).** 0020A7A0 (the status background,
  about 155,000 instructions per call), 00213A00 and 001FCF30 dominate the
  page cases. In the default run the native side replays the oracle's
  journaled stores of those calls (after the entry check proved identical
  memory and arguments) instead of running them, and the oracle memoises
  them: a call whose stack pointer, argument and every value it read on a
  recorded run (first reads, instruction fetches included) are unchanged
  replays the recorded stores. EM_TEST_FULL=1 (and EM_A01UI_RUNALL=1)
  turn both off; the full run passes with every callee run on both sides.
- **Only executions the EE completes** (the render lane's rule); an
  integer division by zero inside the lane's routines (0022BBC0 op 1) is
  refused on the oracle side and must fault (code 6) natively: one case.

Case groups (full / quick):

| group | full | quick | what |
|---|---:|---:|---|
| page 3 (00214020) | 483 | 50 | the a01_s1 / a01_s2 / a01_05 status block: state 0 with request 3 and ids across every category bound (the route's 0x48 included), found at slots below and above 4, not found; states 1..4 with Triangle / Cross edges, repeats 0x1000 / 0x2000 / 0x4000 / 0x8000, category wraps, list-cursor ends (t[0x17] 0 / 7 / 9), a blank Cross, pages +0x1C +-11 / 12 (00213CC0's return), countdowns, page counts 1 / 2+ and the last page, blink bit; states 5 and 0xFF |
| page helpers and packets | 321 | 60 | direct 00213F30 (all categories and caller ranges), 002131B0, 002134C0 (every pad bit pair, both a1, page numbers), 00210A00 / 00210C00 (a0 0, 1, 2, -1; pads), 00207D90 (all lanes negative; b0 negative with b2 not; 16-bit edges), 00208040 (both slots, 64-bit rgba, a NaN and 1e10 inputs), 00210030 (both a1, a signed-zero chain), 00210F30 (areas 0x12 / 0x15, other floor / angle, nonzero position), 00211400 (counter bits, every kind, gates open / closed / half) |
| page 1 (0020F950) | 291 | 30 | modes 0..2 and 3 / 0x80: owned-map patterns and current maps (bit 6 of D_008106CD set; nibble 15 with the search running out), requests, Triangle / Cross, repeats with wraps and no set map, areas 0x12 / 0x15 / 1, current / other map |
| page-1 nodes (002101C0) | 197 | 25 | mode 0 allocates the nodes (both sides), then the callback as the pool runs it (D_00275B44 / 48 = node, D_00275B40 = node + 0x110) in states 0, 1 (through 0), 2, 3, 7; zoom mode, selected / current map, every pad bit and pair, zoom values landing exactly on 2 and 10, pans inside and at the map-10 limits, areas 0x12 / 0x15, a nonzero t+0x44 (D_00810174) in the view centre, cap 0 / 2 / -1 |
| page-1 sequences | 6 | 1 | mode 0 then 1, 3 or 5 mode-1 / Cross / mode-2 frames: 001B0000 ticks the 22 nodes through the original 002101C0 |
| burn node (0022BBC0) | 121 | 40 | the a01_s3 node as captured, 3 frames, variants 0..9, states 0 (init per variant, cap), 2, 3, 4, 0x80, scene stop; crafted timelines (every op, each gating variant, op 1 with a negative and a zero divisor); ramps; emitter phases 1, 0x14, 0x15, 0x28, 0x29, 0x3C, 0x4F, 0x50, 0x78, -3 for variants 0, 1, 2, 4, 6, 9; bursts 0..5 and a bad kind after a good one; fades below 0; ring ages / kinds around a captured emission and off to the side (alpha 0), wraps at 0x40 / 0x3F |
| effect helpers | 284 | 60 | 0022B7A0 over e[0xD] 0..10 x q[3] x q[2] / q[0xD] / D_008101E4; 0022B700 caps; 0022BB70 over 0, 1, 12, 13, 25, 26, 64, -1, -13, -14 |

- **Stand-in model bank.** The map page's model bank (asset slot
  D_0028A570[0], loaded with the page) is not resident in any end-of-beat
  image. Node cases that set models up use another resident slot's bank
  (slots 1, 5 or 11, by trial) as a test input; 9 full-mode cases whose
  foreign model the original cannot run through are skipped and counted.
  So 002101C0's model paths are verified on stand-in models, not on the
  map models.
- **Coverage.** All 221 conditional branches of the 17 routines, both
  ways (440 of 442 outcomes; the test fails on a missing one), except 2
  unreachable by construction: 0022BBC0's two seq[0xD] >= 10 exits (the
  original divides by its caller's s1 there; only the native fault path
  exists, tested in the fault group). (Round 1 also listed 0020F950's
  mode-0 search running out as unreachable; that was wrong, see section
  2: it is reached with a current-map nibble of 15 and is now pinned.) The default run keeps every outcome
  through `QUICK_PINS` (the greedy outcome cover the `cover` command
  prints) plus `EXTRA_PINS` (the data conditions the mutation check
  needed, section 4) and a fixed-seed sample; the test asserts that every
  pin is one of the generated items.
- **Faults (native only, 60):** no views (code 4) at the 16 entries that
  read memory; a latched fault refuses at all 17 (no call, no store);
  `call` NULL (code 1, nothing written) at the 12 entries that call; a
  failing callee (code 2, no later call) at the first, second, middle and
  last call of 002131B0, 002134C0 and 0022BBC0; code 6 for 00213F30's
  caller registers (00214020 state 1, category 7, decrement) and
  0022BBC0's period (seq[0xD] 10) and burst (first kind 7).

Measured (Apple M1, 4 worker processes, a shared machine with load
average about 30): default run 9.1 s wall, user 17.3 s, sys 0.5 s (266
cases, 4,228 callee entries checked); full run 344.6 s wall, user 605.8 s,
sys 7.8 s (1,694 cases, 30,810 entries). The review measured the previous
case set at 10.8 s wall, user 19.5 s (default) and 168.8 s wall, user
523.7 s (full) on a quieter machine: wall time depends on load. The
default run's CPU time is above the ~10 s target: the status background
0020A7A0 runs as original code in the oracle on every page frame that
misses the memo.

## 4. Mutants (bounded)

`python3 tools/test_area01_ui_reference.py mutants [names]` builds each
single-operation mutant of the two C files and runs the default case set.
42 mutants: 40 killed, 2 proven equivalent, none surviving the default
run. The first 37 are the lane's own; the last 5 are the survivors of the
review's 45-mutant sweep (4 surviving every case, 1 killed only by
EM_TEST_FULL=1), each now killed by the default run through a pinned
case in `EXTRA_PINS`:

- `7D90_b0_zext` (lane b0 zero- instead of sign-extended): killed by
  00207D90(1, -1, 0, 0, 0) (RAM: the packet's +0x34 word). The all-negative
  case could not kill it: b2's sign bits cover the same bits.
- `01C0_vy` (002101C0 stores 0 instead of t+0x44 to 0x700038A4): killed by
  node 10, state 1, zoom mode 1, t+0x44 = 3.0 (entry check at 001029C0,
  scratchpad 0x700038A4..). No earlier case set t+0x44.
- `F950_floor_mask` (mode 0 t[0x14] mask 0x30 as 0x70): killed by mode 0
  with D_008106CD = 0x40 (RAM 0x810144).
- `BBC0_wrap1` (variant-1 wrap at 0x50 instead of 0x51): killed by
  variant 1 with every emitter at phase 0x4F (entry check at 00103230,
  RAM 0x7B1140..).
- `B7A0_t17` (0022B7A0 variant-9 table for q[3] = 17 one row off): killed
  by the pinned pick item k 9, q[3] 17, q[2] 0x21, q[0xD] 1, mode 3
  (RAM); before, only the full run reached it.

- Killed (the first failing check in parentheses): 00207D90 header qwc
  and lane-3 shift (RAM); 00208040 rgba zero-extension and x/z shift (RAM);
  00210030 bias constant (RAM) and the `0 + x` add (only with a -0 chain:
  `EXTRA_PINS`); 002101C0 colour swap, 0.08533333 constant and the
  indirect call's argument (entry check); 00210F30 `164 - x` as `+` (only
  with a nonzero t+0x40: `EXTRA_PINS`) and the pi/2 constant one unit low
  (only with a small angle, where the unit survives the subtraction:
  `EXTRA_PINS`); 00211400 a3
  of kind 4; 0020F950 wrap value, wrap compare and help group; 00213F30
  range; 00214020 slot sign test and the request clear in state 3 (needs
  the request byte set: `EXTRA_PINS`); 002134C0 next-arrow compare and
  frame size (entry check: the buffer address); 002131B0 message line;
  0022BBC0 ramp constant, LCG constant, ring wrap bound (needs w+0xB0 =
  0x3F: `EXTRA_PINS`), age steps, emission phase, scale 1.5, op 2 byte,
  burst table, buffer offset, scroll step, age cutoff; 0022B7A0 table;
  0022B700 cap compare; 0022BB70 row divisor; and the five above.
- Proven equivalent: 002101C0's zoom clamp `f < 2` as `f <= 2` (at f == 2
  the store writes the value already there; the case landing exactly on 2
  runs); 00214020's last page `lh(count word) - 1` as `count - 1` (only the
  low 16 bits are stored, and the count's low halfword is its value
  modulo 2^16).

## 5. Known gaps

- The captures are end-of-beat images: the pages were closed and the burn
  ring empty (every age -1). Page states, requests, pads and ring contents
  are rebuilt from the captured status block and node, so the "capture"
  shapes are the real call shapes on real data, not replays of recorded
  frames; no per-frame capture of the pages exists.
- 002101C0's model setup and draw run on stand-in model banks (section 3).
- The EE's integer division by zero is not modelled; 0022BBC0 op 1 with a
  zero value faults instead (code 6).
- The replayed / memoised callees (quick mode) are checked for argument
  and memory equality at entry, not re-executed; the full run executes
  them.
- The no-views check does not apply to 00210A00 (it only calls).
- 001CD070 / 001CD2B0 stay callees; the render lane's translations of them
  are not bound here.

## 6. Notes for the lead

- `python3 tools/test_scene_no_shadow.py` passes with these files. No file
  here names a migrated byte by its address, but 00213F30 reads the owned
  bytes D_00810700 + id + 0x5C3, which for id 0 is D_00810CC3 (a migrated
  progress byte). If the lead wants that reach listed, the REACHERS entry
  is: `0x00810CC3: {"game/em_area01_ui_pages.c": "00213F30's owned-id reads
  D_00810700[id + 0x5C3] (id 0 = D_00810CC3), AREA01 level-2 lane UI, not
  bound"}`.
- Binding: the page core's fault for pages 1 and 3 (AREA11 host) can call
  `em_area01_ui_0020F950` / `em_area01_ui_00214020`; the node callback is
  `em_area01_ui_002101C0`; 0022BBC0 is a pool callback id without an
  AREA11 row. Every callee listed in `UI_*` (em_area01_ui_internal.h) must
  be bound or fail-stop.

## Known gaps from the close-out spot check (2026-09-26)

Not mistranslations; test blind spots recorded instead of another sweep round.

- residual_BBC0_init38: in 0022BBC0's init loop, clearing byte w+i+0x39 instead of w+i+0x38 survives the default run: no state-0 case leaves nonzero bytes at w+0x38.. before the loop. The translation passes the verifier's killing input (build/area01/verify2_UI/init38_kill.py).
