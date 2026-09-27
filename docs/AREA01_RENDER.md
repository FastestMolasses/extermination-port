# AREA01 render lane

Lane RENDER of the level-2 (AREA01) side track. The census delta
(`../Extermination/build/s87/census/a01_delta.json`) lists 21 main-line new
boot functions in the subsystems frame_update, render_vif, fx_render,
gs_upload and hud_objects (12,536 bytes). Main line means not side-beat-only
and not exit-or-AREA00-only. The census subsystem names are labels only.
Nothing below relies on them.

- 16 of the 21 are translated here, in four modules grouped by caller.
- 5 were already translated in the port for the first level. They are not
  duplicated or edited. They are checked on AREA01 inputs in a separate test
  (section 4).

Nothing is wired. Each module is a standalone translation. Each callee it
does not translate is a worker named by the original address.

## 1. Modules and functions

Every module reaches the original's bytes through views keyed by original
address (EE RAM, and the scratchpad at 0x70000000). The shared helpers are in
`src/game/em_area01_render_mem.h`. Faults are fail-stop. The first fault
is latched with a function address, a code and a detail word. What the test
checks of this contract (section 3, faults) and what it does not:
- A NULL worker faults with code 1 at entry, naming that worker, before
  anything is written or called. Tested at all 16 entries, for every worker
  each can reach.
- A failing worker (a negative result) faults with code 2, naming that
  worker; the entry returns −1 and makes no further worker call. Tested at
  each of the 13 entries that have workers: quick mode fails the first call
  of each distinct worker plus the first, second, middle and last call,
  full mode every call. Not tested: that no memory is written after the
  failing call.
- An address no view covers faults with code 4. The detail is that
  address, and the function is the original routine that made the access.
  Tested:
  - at all 16 entries, with no views and with views whose bytes are NULL:
    the detail must be the first data address the original routine
    accesses;
  - two accesses whose end would pass 2^32;
  - a RAM view that ends exactly at 001E8B90's first access (0x810700);
  - a RAM view that starts 4 bytes after 001CD2B0's store of n (0x275C00).
    The fault must name 001CD2B0, not the inline 001CD180 body that ran
    just before.

  The view-edge cases (section 3) check the other side: accesses that
  start or end exactly at a view's edge are served. Not tested: any other
  partial-view world, and the fault record of an access the EE cannot
  complete in the first place. The module also resets the function address
  after a worker call returns (001D4FC0, 001E8B90, 001E9E60) and after
  001D5BD0's nested bodies. No test can observe those resets. A worker gets
  `workers.ctx`, never the module state, so it cannot change that address.
  After the nested bodies, 001D5BD0's next access is always inside
  001D5A70's body, which sets its own address first.
- A latched fault refuses every later call: −1, nothing written, nothing
  called. Tested at all 16 entries.

COP1 and VU0 arithmetic uses `em_ee_float.h`. The VU clip test follows the
rule in RENDER_CONTEXT.md section 4, which is not measured (see section 5).

### vif (`em_area01_render_vif.{h,c}`); caller 001D5370

| addr | decomp source | what it does |
|---|---|---|
| 001D5BD0 | byte-matched C | Loops over the records of the table at `*D_0028A5A4` (count = the table's first word, 0x860-byte records from +0x10). It calls 001D5A70(rec, rec+0x40) for each. Result 0 runs 001D4FC0(rec). Result 0xFF does nothing. Any other result runs 001D4FC0(rec), then 001D5170(rec). |
| 001D5A70 | asm words | Takes the three quadwords at a1+0x40, +0x80 and +0xC0, with w replaced by 1.0, and multiplies each by the matrix 001CD370(0) (context +0x2240). Each product gets the clip test against its \|w\|. Returns 0xFF when the three flag sets share a bit, 0 when all three are empty, else 1. a0 is not read. |
| 001D4FC0 | NEARMISS | Builds a channel-3 chain (see below). |
| 001D5170 | NEARMISS | Builds the same chain, but calls 001D4750(3) in place of the tag and copied block, and uses D_00237720 in place of D_00237450. |

001D4FC0's chain, in order:
1. A tag (id 0x10, qwc 0xE, addr 0) with the 0xE0 bytes of D_002513D0 copied
   behind it by 00121870. The cursor is advanced by 0xF0 before the copy.
2. 001D2090(3, D_00237450).
3. A 0x30 tag to D_00816A40 + (context +0x9C << 7) with qwc 8, then
   001D1F80(3, 2, 1, that slot).
4. One 0x30 tag per chunk of at most 0x1F8 vertices of rec +0 over the data at
   rec+0x40. Each tag has qwc = chunk * 0x82, and the address advances by
   chunk * 0x820.
5. A 0x60 end tag.
6. 001CAAC0 with the stack quadword (rec +0x34, +0x38, +0x3C, 1.0), the first
   tag's address, 0x60 and the context.

The translation loads the cursor word again before every tag store, in the
original's order. It loads D_00275670 again wherever the original does:
once at the start of each routine, then in the common tail after 001D2090
returns, before every chunk tag, and before the end tag.
- A cursor reload is observable only when an earlier store of the same tag
  lands on the cursor word. The tag-alias group (section 3) reaches the
  shared tag helper's three reloads (the +0x1C, +0x18 and reloc shapes) and
  step 1's reload before its addr store (first19). Step 1's other two
  reloads, before the qwc store and before the cursor store, cannot be
  observed by an execution the EE completes. The survivor table gives
  both arguments (`r3_vif_4fc0_inline_cached_qwc`,
  `r4_vif_4fc0_inline_cached_cur`).
- A D_00275670 reload is observable only when a store since the previous
  load lands on D_00275670. Each of the three reloads has a D_00275670-alias
  shape (section 3), an execution the EE completes in which such a store
  happens:
  - `body`: 001D2090's own tag, for the reload after it returns;
  - `slot`: the slot tag, for the chunk loop's reload;
  - `slot0` / `slotneg`: the slot tag with no chunk, for the reload before
    the end tag.

### gs (`em_area01_render_gs.{h,c}`)

Callers:
- 001E3D90, 001E4CE0, 001E7050, 001F8350 and 0022BBC0 call 001CD070 and
  001CD2B0 as a pair.
- 00158BD0 and 00158D30 call 001F4A10.
- 00158590 calls 001F4CC0.

| addr | decomp source | what it does |
|---|---|---|
| 001CD070(p, mask) | NEARMISS | Applies the clip test to p through 001CD370(0). If a flag also set in mask is set, it returns 0xFFFFFF. Otherwise it projects p through the matrix at 0x70003AC0 with the quadword at context +0xA0 (the w term is min(ca0.z + ca0.w·clip.w, ca0.x), then max 0). It stores 12.4 x, y and z plus that clamped w term at 0x70003600, sets D_00275C04 = float_to_int(clip w), and returns the z word. |
| 001CD180(f12, f13) | NEARMISS | Stores (0.5·f12, 0.5·f13, (float)D_00275C04) at 0x70003610. It multiplies them by 0x70003A40 with that matrix's z row x/y cleared (0 − 0 by VSUB), divides by w and converts x/y to 12.4. The result is stored as the whole quadword. It returns 0 when the box around the 0x70003600 point fails one of four fixed bounds (x − hw ≥ 0x9001, y − hh ≥ 0x8701, x + hw < 0x7000 or y + hh < 0x7900). Each sum and difference wraps in 32 bits and is compared as a signed word. Otherwise it keeps the larger half extent at 0x70003610 and returns (it >> 3) + 1. |
| 001CD2B0(f12..f15) | byte-matched C | Sets 0x70003680 = 0 and D_00275C00 = 001CD180(f12, f13). When the result n is nonzero, 0x70003680 = 1 − min(1, (n − f14 < 0 ? 0 : (n − f14) / f15)). n − f14 = 0 still divides. With f15 = +0, 0/+0 saturates to +MAX, the clamp gives 1 and the result is 0.0. With f15 = −0, 0/−0 saturates to −MAX, the clamp does not apply, and the result is 1 − (−MAX) = 0x7F7FFFFF. It returns 0x70003680. |
| 001F4A10(obj, v) | NEARMISS | Calls 001C7900(obj, v.xyz · s, 0x3F5, 3), where b = v.w / 128 and s = (b + (rand / 2^31)·b) / 32. Then it calls 001D3990(001C6120(*D_0028A56C, 0xC)) and writes a 0x60 end tag. Last it calls 001CB760(D_007635C0, key, packet start). The key is float_to_int(0x4D7FFFFF / w word), where the w word is the 001CD070-style projection of the object `*D_00275B44` +0xB0. |
| 001F4CC0(a0, a1) | byte-matched C | Calls 0021B9A0(2, 0, 100000), then 0021B9A0(3, 0, 1000000), then 001F4BF0(a0, a1), then 0021B9A0(1, 0, 0). |

### hud (`em_area01_render_hud.{h,c}`)

Callers:
- The player and actor floor code calls 001E8B90 (12 callers, e.g. 00187350).
- 0015A2C0 calls 001E9E60.
- The handlers are D_00255434 entries that 001EA240 calls.

| addr | decomp source | what it does |
|---|---|---|
| 001E8B90(p, f12) | NEARMISS | Does nothing if the byte D_00810700 is 0x15 or 0x10. It checks each of the four 0xA060-byte records at `*D_00275C20` whose word +0x54 is nonzero and that holds p. "Holds" means x0 < x ≤ x0+w, z0 < z ≤ z0+d and y < y0+11, using the EE compares (a sub-normal counts as 0). For each such record it maps p to a 32×32 cell (float_to_int, clamped 0..31). In the grid at +0x9060 (column stride 4, row stride 0x80, neighbours clamped) it adds −7·f12 to the cell, −5·f12 to its four edge neighbours and −3·f12 to its four corner neighbours. The adds run with the column offset outer and the row offset inner. That order is visible where clamped neighbours fall on one cell. At column 0 / row 31, for example, that cell gets the edge add from (column −1, row 0), then the corner add from (−1, +1), then the centre add, then the edge add from (0, +1). The mapped x and z replace p's for the later records. |
| 001E9E60(a0, a1) | NEARMISS | Works on the record R = `*D_00275C1C` + a1·0xA060 and builds three kinds of packet (listed below), then calls 001CB950, 001CB6B0 and 001CB760. |

The four effect handlers (001EAF00, 001EB020 and 001EC270 are NEARMISS in
the decomp; 001EAF80 is byte-matched C):
- The handler at 001EAF00 calls 001CFB50(D_0081F8F0, 0, a0, work+0x54,
  work+0x5C, 1.0, 1e-6, 5.0), then 001CFBE0(a1, 1, D_002557D0, D_0081F8F0,
  0).
- The handler at 001EAF80 does the same with f16 = 9.0, then makes two
  001CFBE0 calls, with the tables D_00255860 and D_002558F0 (both kind 1).
- The handler at 001EB020 runs three rounds. Each round sets f13 =
  ((work+4) >> 16 & 0xFFFF) / 65535 + 1e-4 and work+4 = work+4 · 37 + 11,
  with f16 = 9.0. The tables are D_00255980, D_00255A10 and D_00255AA0 (all
  kind 1).
- The handler at 001EC270 runs two such rounds with f16 = 5.0. The tables
  are D_00256790 (kind 1) and D_00256820 (kind 0).

001E9E60's packets:
1. Six 0x1A-quadword GIF packets (tag 0x6C188000). Packet s copies the +0x60
   quadwords (8 rows) of the 0x200-byte segments s, s+1 and s+2 of R. Word
   +0x190 is 0x14000000 for the last packet and 0x17000000 for the others.
2. A 5-quadword packet holding 0x70003AC0.
3. A 9-quadword packet holding:
   - R +0x48..+0x50;
   - 60 + a0 +0xB4;
   - blend = R+0x10..+0x18 · (1 − t) + D_0026E9B0..B8 · t, with t = a0 +0x80,
     computed by MULA then MADD;
   - R +0x1C · a0 +0x8C;
   - R +0x3C/+0x38/+0x40/+0x44;
   - D_008105D0, a fixed register pair, and context +0xA0/+0x2220/+0x2230.

After the packets come 001CB950 (the word pair picks on the byte R +0x5C
== 1, all eight bits compared),
001CB6B0(…, 8, D_00275674 + 0x720) and 001CB760(…, D_002345E0).

In the four handlers (captured calls), a0 is node +0xD0, a1 is passed through to 001CFBE0, and work is
`*D_00275C34`, which is node +0x1F0 in the captures.

### frame (`em_area01_render_frame.{h,c}`); caller 0015BA50 for a0 byte 5

0015B610(a) is asm words. Let b be the scratchpad byte 0x70003B8D.

When b is neither 0 nor 4 and 00182B30(a, b) returns 0, it sets the state by
+5 and then calls 00182D70(a):
- +5 == 3: +4 = 4, +5 = 0xC, +6 = 0, +0x1F0 = 0x17.
- +5 == 1: 00174A50(a, 8.0), then +4 = 4, +5 = 0, +6 = 0, +0x1F0 = 0x41.
- otherwise: the same stores without the call.

Otherwise +5 selects the routine: 0 → 00183240, 1 → 00183250, 2 → 001833F0,
3 → 00183440, 4 → 001834E0. Any other value calls nothing.

## 2. Where the decomp's NEARMISS C differs from the instructions

The translations follow the instructions. These differences were found and
the oracle confirms them. The lead may want to carry them into the decomp's
FINDINGS or the NEARMISS C.

- **001E8B90.**
  - The record stride is 0xA060. The C uses 0x2061.
  - The corner neighbours get −3·f12 and the edge neighbours get −5·f12. The
    C gives −3 to the vertical neighbours only.
- **001E9E60.**
  - Each packet copies segments s, s+1 and s+2 from the record base. The C
    adds its advancing base again, giving segments 2s+1 and 2s+2.
  - The +0x20 quadword is a blend by the single scalar a0 +0x80 between the
    record vector and D_0026E9B0..B8. The C reads a0 +0x80/+0x84/+0x88 per
    axis and uses a different formula.
- **001CD2B0 / 001CD180.** 001CD180 takes f12 and f13, which 001CD2B0 passes
  through unchanged. Only the extern prototype in the byte-matched
  `src/func_001CD2B0.c` says `(void)`, which is harmless for matching. The
  NEARMISS `src/func_001CD180.c` already defines
  `func_001CD180(float width, float height)`, so that file needs no change
  for this point (the parameter names are the decomp's labels).
- **001D5A70 and 0015B610** are asm words in the decomp. They are described
  here from the instructions.

The mutation check in section 3 shows that the test rejects each of the C's
versions of these points.

## 3. Verification (original-instruction oracle)

`python3 tools/test_area01_render_reference.py`
- The oracle runs each original routine with the shadow-route interpreter
  (RouteEE: every COP1 and VU0 op from `tools/ee_float_model.py`). It adds
  the clip test, the CLIP register read and three MMI word ops.
- It runs over the recorded AREA01 captures,
  `../Extermination/build/s87/route_a01/<beat>/{eeMemory,scratchpad}.bin`,
  12 beats. Their code bytes are checked against the pinned ELF.
- Every worker also runs as original code:
  - On the oracle side, nested inside the run, with its arguments logged.
  - On the native side, in a second interpreter that shares the native
    module's RAM and scratchpad bytes, logged the same way.
  - The exception is 0015B610's player routines, which are scripted
    boundaries on both sides.
- **Only executions the EE completes.** Both interpreters apply the EE's
  access rules to every load, store and instruction fetch, the callees'
  included. The access must be aligned to its size; a quadword access drops
  the low four address bits first, as the EE does. The address must also be
  in the TLB map the AREA01 captures hold. That map was read from the
  48-entry TLB in each route_a01 PCSX2 save state and is identical in all
  12:
  - main RAM at 0x00080000..0x01FFFFFF;
  - its uncached view at 0x20080000..0x21FFFFFF;
  - its uncached-accelerated view at 0x30100000..0x31FFFFFF (in all three,
    virtual & 0x1FFFFFF is the physical address);
  - the scratchpad.

  The TLB also maps the EE, VU and GS register pages (0x10000000..
  0x1000FFFF, 0x11000000.., 0x12000000..), 0x1E000000..0x1FFFFFFF and
  0xFFFF8000..0xFFFFFFFF (kernel memory, physical 0x78000..0x7FFFF). They
  are outside the model, so touching them also refuses the case. No valid
  entry maps any other address of the TLB-mapped segments, for example
  0x00000000..0x0007FFFF, 0x02000000..0x0FFFFFFF, 0x10010000..0x10FFFFFF,
  0x20000000..0x2007FFFF, 0x30000000..0x300FFFFF, 0x32000000..0x6FFFFFFF
  or 0x70004000..0x7FFFFFFF (the interpreters' private stack at 0x7F000000
  aside; nine entries at 0xE004C000..0xE005DFFF are
  marked invalid): an access there raises a TLB exception on the EE.
  0x80000000..0xBFFFFFFF is not translated by the TLB; the model refuses
  it as well, and no argument below relies on it. A case whose oracle run
  breaks either rule fails ("the original cannot complete this case").

  Every case in both modes passes this rule. The rule showed that two
  round-2 tag-alias shapes were not EE executions, so they were dropped:
  - 001D5170's chunk tag with the cursor at context +0x1C: a later store is
    misaligned.
  - 001D4FC0's inline first tag at +0x18: its qwc store goes to address 0.

  The round-2 review's probes for the two cached-D_00275670 mutants (cursor
  0x27566C, and 0x275670 − 0x140 for 001F4A10) fail the rule too, through
  an unmapped id store and a misaligned load. The cases below that replace
  them pass it.
- **Entry check (every worker call, both kinds of worker).** Before the
  native side runs (or scripts) worker call k, it must be in exactly the
  oracle's state at the oracle's call k:
  - the logged arguments (every argument register and stack vector the
    worker's signature takes) are equal;
  - all 32 MiB of RAM are equal;
  - all 16 KiB of scratchpad are equal.

  How:
  - The oracle interpreter journals every RAM and scratchpad store in order
    (the bytes stored; its private stack is outside both regions). It
    records the journal length at each worker entry.
  - The native side keeps an expected image equal to the case's starting
    RAM. At entry k it replays the oracle's journal from entry k − 1 up to
    entry k onto that image.
  - The native RAM buffer is write-protected page by page (16 KiB pages).
    The first write to a page after a check faults once into a small
    handler, which records the page and unprotects it. That covers the C
    module's writes and its worker interpreter's writes alike.
  - The check compares the scratchpad in full. It compares RAM on exactly
    the pages written since the previous check: the pages the replayed
    journal touched and the pages the native side wrote. Both images were
    equal at the previous check (or at the start), and neither changed on
    any other page, so equal candidate pages mean all 32 MiB are equal.
  - Any difference fails the case with the call index, the worker address
    and the first differing addresses. An extra native call, or fewer
    calls, also fails. The count of checked entries must equal the oracle's
    worker count.

  Why: the native workers run the original callee on the native memory. A
  wrong native store that a later callee overwrites (or that a callee reads
  and then happens to repair) would vanish from the end state. The entry
  check sees it at the first call after the store.

  Checks on the page bookkeeping itself:
  - Each worker process starts with a self-test: one write by C code and
    one by Python, the latter across a page boundary, must report exactly
    their three pages.
  - `EM_A01R_FULLCMP=1` also compares all 32 MiB at every entry check, end
    check and buffer reset, and fails if the page compare ever disagrees.
    The final quick mode passes with it (25.6 s wall, user 49.8 s, sys 1.1 s,
    load ~127).
  - The mutation rerun below re-kills every planted-store mutant with the
    page compare. One of them, `P_far_overwrite`, stores to a page that
    neither the oracle's journal nor the routine otherwise touches, and the
    native code undoes the store after the next worker call. Only the
    write tracking can point the check at that page.
- **Unpassed registers are poisoned.** Before each native worker call every
  register the call does not pass is overwritten with 0xA5…: GPRs (both
  halves) except zero/gp/sp/ra, HI/LO, all FPRs and the FPU ACC,
  VF1..VF31, the VU ACC and Q. The oracle's callee sees the caller's live
  registers instead. All cases still pass, so no worker in any case reads a
  register outside its logged arguments in a way that changes what it
  writes or returns.
- **End check.** A case passes only if all of these are identical after
  the last step:
  - the return value;
  - the worker log (every argument, and every result the routine uses);
  - all 32 MiB of RAM (compared the same way as at entry: the pages the
    native side wrote and the pages the oracle's journal touched since the
    last check);
  - all 16 KiB of scratchpad.

  So every packet byte a builder writes, and every byte its callees write
  from what it passed, is compared, both at every call boundary and at the
  end.
- **No per-case image copies.** Each of the at most four worker processes
  keeps its four RAM buffers for the whole run: the case start, the
  oracle's RAM, the expected image and the native RAM. A buffer knows
  which captured image it holds and the pages where it may differ from it.
  A reset copies only those pages, plus, when the beat changes, the pages
  where the two captured images differ (59 to 535 of 2048). Work is handed
  out sorted by beat. This replaces round 2's three fresh 32 MiB copies per
  case, and the full-RAM compare at every entry, which caused its large
  system time.

Case groups:
- **capture.** 201 cases: each routine in its real call shape on every beat.
  - 001D5BD0 as captured. a01_07 is skipped because its image is already
    AREA00.
  - The 001F8350 pairing: 001CD070(`*D_00275B44` +0xB0, 0x30), then
    001CD2B0(6, 6, 96, 32).
  - 001F4A10 on the beat's 00158D30/00158BD0 node with both 0x700038B0 word sets.
  - 001F4CC0 with 0x700038A0 / 0x700038B0.
  - 001E9E60 on every 0015A2C0 node, with a1 = node +0xE.
  - The four handlers on every live 001EA240 node, with work = node +0x1F0.
  - 001E8B90 at the player's position.
- **unit.** Perturbed state:
  - record counts across the 0x1F8 chunk boundary, and negative counts;
  - moved clip points, giving all three 001D5A70 results;
  - slot indices;
  - points inside, on and outside the 001E8B90 record boxes;
  - D_00810700 bytes 0x15/0x10;
  - live and dead records;
  - blend, seed and extent words;
  - 001CD2B0 f14/f15 ranges, including f15 = 0 and negative values;
  - special float patterns.
- **001CD2B0 t = 0 and v = 1 boundaries** (36 cases). On every main beat
  whose 001F8350 pairing gives a nonzero 001CD180 result n (a01_01, a01_02,
  a01_05, a01_06; n is 0 at the others), the pairing is rerun nine ways:
  - f14 = (float)n with f15 ∈ {+0, −0, 5.0};
  - f14 = n + 1 with f15 = +0;
  - f14 = n − 1 with f15 = ±0;
  - f14 = 0 with f15 = (float)n one unit below, equal, and one unit above
    (as bit patterns), so v = t / f15 lands just above 1.0, on it, and just
    below it. On a01_01 (n = 51) the first gives v = 1 + 2^−23
    (0x3F800001), which the clamp turns into 1 (result 0). A clamp that
    let 0x3F800001 through would return 0xB4000000.

  At t = 0 the original divides.
  - With f15 = +0, 0/+0 saturates to +MAX, the clamp gives 1 and the
    result is 0.0.
  - With f15 = −0, 0/−0 saturates to −MAX, the clamp does not apply, and
    the result is 1 − (−MAX) = 0x7F7FFFFF.
  - A "t ≤ 0 counts as 0" rule would return 1.0 in both (the round-1
    review's c_lt → c_le mutant).
- **001CD180 bounds** (112 cases; quick mode runs a covering 24). The
  extents (hw, hh) do not depend on the centre, so one oracle run with the
  centre far out gives them (it returns at the first test and leaves hw, hh
  at 0x70003610). Each case then places the centre so that x − hw, y − hh,
  x + hw or y + hh lands exactly on its bound, or one past it. The bounds
  are 0x9000 and 0x8700 (last values inside the first two tests) and
  0x7000 and 0x7900 (first values inside the last two). The other
  coordinate sits at 0x8000. Main beats × extents (6, 6) and (40, 24) × 8
  positions.
- **Direct 001D5A70** (126 cases; quick 24). The result word itself is
  compared: points as captured, one or all far out, and placed around the
  point the clip matrix maps to clip x = y = 0. The run must see all three
  results 0, 1 and 0xFF.
- **The quadword at context +0xA0** (189 cases; quick mode runs the 27 on
  a01_00 plus a covering sample, 33 in all). Every capture holds x = 255
  there, so nothing else changes it.
  - Each case sets x to −5, −0.5, +0, −0, 0.001, the smallest sub-normal or
    a positive NaN pattern (0x7FC00000), or sets (z, w) to (−3, 0.25) or
    (1000, −2).
  - It then runs 001CD070 + 001CD2B0 (the 001F8350 pairing), 001F4A10 or
    001E9E60 (which copies the quadword into its packet) on each main beat.
  - The projection takes the smaller of the w term and x first, then the
    larger of that and +0. The two orders differ only when x < 0, which
    only these cases reach.
  - The VU min orders raw patterns, so x = 0x7FC00000 counts as a huge
    positive number and the w term is kept. A host float min would pick
    the NaN, which the 12.4 conversion then saturates. On a01_00 that
    changes 001F4A10's key.
- **Tag alias** (56 cases; quick mode runs the 8 on a01_00 plus a covering
  sample). The tag helper, and 001D4FC0's inline first tag, load the cursor
  word (context +0x1C) again before each of their four stores: the id byte
  at +3, the addr word at +4, the qwc halfword at +0, then cursor += 0x10.
  A reload is observable only when an earlier store of the same tag lands
  on the cursor word. Each case asserts that the aliasing store happened.

  | shape | sites | what happens |
  |---|---|---|
  | +0x18 | 001D5170 slot and first chunk tag, 001D4FC0 slot tag | the addr store overwrites the cursor, and the qwc store follows it |
  | +0x1C | the two slot tags | the id byte 0x30 replaces the cursor's top byte, giving the uncached-accelerated view of the same RAM, and the qwc halfword replaces its low half |
  | reloc | the two slot tags | +0x1C with the context copied to X = context + 16 MiB (D_00275670 = X): the reloaded cursor 0x30xxxxxx reaches the copy of that address in the low 16 MiB, not X +0x20 |
  | first19 | 001D4FC0's inline tag | the cursor starts at context +0x19, with the context copied to 0x1A000E8. The id byte 0x10 replaces the cursor's low byte; the re-read cursor is aligned and the tag completes, while a stale cursor would put the addr word at X + 0x1D |

  - For the slot and chunk sites, the workers before the site append a
    fixed amount. So the start is the site's normal cursor offset
    subtracted from the target, and the test asserts that the site was
    reached there.
  - The context copies take the context's first 0x3000 bytes. They hold
    every context offset these routines and their callees read in the
    normal runs; the highest is +0x246C.
  - At +0x1C, reloc and first19 the native world gets one more view: the
    same RAM at 0x30100000..0x31FFFFFF, as the TLB maps it.
- **D_00275670 alias** (91 cases: 13 shapes on the 7 main beats; quick
  mode runs the 13 on a01_00 plus a covering sample, 19 in all).
  D_00275670 changes during a routine only when a store lands on it. Each
  shape places a store there, before one of the routine's reloads, and
  each case asserts that the store happened.

  | shape | routine | the store on D_00275670 | the reload it reaches |
  |---|---|---|---|
  | slot | 001D5170, 001D4FC0 | the slot tag's addr word (cursor 0x27566C) writes the slot address. Context +0x9C makes the slot 0x1816A40, which holds a copy of the context | the chunk loop's: the chunk tags and the end tag go to the copy's cursor |
  | slot0, slotneg | 001D5170, 001D4FC0 | the same, with the record count set to 0 or −5, so no chunk runs | the reload before the end tag, and 001CAAC0's context argument |
  | body | 001D5170, 001D4FC0 | 001D2090's second tag (id 0x50, qwc 0) has 001D2090's table argument (0x237720 / 0x237450) as its addr word. The cursor puts that word on D_00275670, and the context is copied to the table address | the common tail's first reload: the slot tag goes to the copy's cursor. (The copy overwrites the table 001D2090 reads; it then reads context bytes there.) |
  | c7900 | 001F4A10 | the context is copied to 0xF08000 with its cursor at 0x275670 − 0x110. 001C7900's fifth tag (id 0x30, qwc 8) makes D_00275670 0x30F00008, the uncached-accelerated view of 0xF00008, which holds a second copy | the reload before the end tag: the tag goes to the second copy's cursor |
  | entry | 001F4A10 | 001D3990's last tag (written by 001D37D0) has the addr word E + 0x40, E being 001C6120's entry (table `*D_0028A56C`, index 0xC). E is set to 0x275614 and the cursor placed so that the word lands on D_00275670, which becomes 0x275654: the context whose cursor word (+0x1C) is D_00275670 itself. 001F4A10's end tag then goes to 0x275654, and its cursor store makes D_00275670 = 0x275664 | the reload after the end tag: the +0xA0 quadword comes from 0x275700 (0x275664 + 0xA0, low four bits dropped), not 0x2756F0. The two quadwords are set to the captured one with x = 255 and with x = 100, so 001CB760 gets a different key. D_0027567C (the moved context's +0x18, which 001CB760 reads) is set to the captured packet cursor |
  | copy | 001E9E60 | the 9-quadword packet is placed at 0x275610, so its +0x60 copy of context +0xA0 covers D_00275670..7F. That quadword's x word is Y, a context copy whose +0x2220 quadword differs, and its y word is the captured D_00275674 | the reload before the +0x2220 copy |
  | copy3 | 001E9E60 | the packet at 0x275600, so its +0x70 copy of context +0x2220 covers D_00275670..7F. Context +0x2220 is (Y, D_00275674), Y a context copy whose +0x2230 quadword differs | the reload before the +0x2230 copy |
  | word48 | 001E9E60 | the packet at 0x275660, so its +0x10 word, the record's +0x48 word, lands on D_00275670. Record +0x48 = Y (a context copy with +0xA0 x = 100) and +0x4C = the captured D_00275674 | the first reload, before the +0xA0 copy |

  With `copy`, `copy3` and `word48`, each of 001E9E60's three D_00275670
  loads has a case in which a store since the previous load changed it.

  The workers' packets before the aliasing store overwrite words just
  below the aliased word. The case asserts nothing about those words
  beyond this: both sides start from the same constructed state, and the
  oracle refuses any access the EE cannot complete.
- **Other re-read words** (21 cases: 3 shapes on the 7 main beats; quick
  9). A routine reads a word again after a call or a store that can change
  it. Each case asserts that the changing store happened.
  - `rand` (001F4A10): the packet start (context +0x1C) is read before the
    00122BB8 call. 00122BB8 stores its LCG state at `*D_0024295C` + 0x58
    (byte-matched decomp C). The context is copied to X = 0x1A00040, with
    D_0024295C = X + 0x1C − 0x58. The cursor s0 is chosen so that the new
    state, s0 · 0x41C64E6D + 0x3039, is the captured cursor. 001CB760 gets
    s0 as the packet start; it stores only its low 28 bits, so an odd s0
    is harmless.
  - `grid` (001E8B90): D_00275C20 is read again for each record.
    - With D_00275C20 = G = 0x26C9B0, record 0's corner cell (col 4,
      row 4) is D_00275C20 itself.
    - p = (5.5, 0, 5.5) in a 32 × 32 box at the origin, with f12 = −2^−126,
      turns that cell into 3 · 2^−126 = 0x01400000. The pointer read as a
      float is sub-normal, so it counts as 0.
    - Record 1 at the new base is live with the same box. Records 1..3 at
      G and 2..3 at the new base are dead. The case asserts that 4 worker
      calls occur.
  - `top` (001E9E60): D_00275674 is read after 001CB950. 001CB950's block
    is placed at 0x275668 (8 bytes off a quadword, as in the packet +8
    group), so its +0xC word store (0x50000002) lands on D_00275674.
    001CB6B0 gets that value + 0x720.
- **Re-read words, sweep 2** (42 items: 6 shapes on the 7 main beats; the
  `count` shape runs only on a01_04 and a01_05, the two beats whose record
  0 builds a chain, so 37 cases run; quick mode picks 12 items, of which 7
  run). A routine reads a word after a store that can change it; each
  case asserts that the changing store happened.
  - `count` (001D5BD0): the table's count word is read once, before the
    loop. The cursor is placed so that record 0's end tag starts at
    base − 4 (base = `*D_0028A5A4`), so its addr word (0) lands on the
    count word. The original still visits every record (48 calls on
    a01_04 and a01_05; a count read again would stop after 4).
  - `recword` (001D5170): the record's vertex count (rec +0) is read after
    the slot tag and 001D1F80. The slot tag is placed at rec − 4, so its
    addr word, the slot address, lands on rec +0 first. The original then
    runs the chunk loop over that count.
  - `endtag_v` (001D5170, 001D4FC0): rec +0x34 (001CAAC0's vector x) is
    read after the end tag. The count is 3 · 0x1F8 and the first chunk
    tag is placed at rec, so the end tag is at rec + 0x30 and its addr word
    (0) lands on rec +0x34.
  - `subject` (001F4A10): D_00275B44 is read after the end tag. The
    context is copied to 0x275B28, so its cursor word (+0x1C) is
    D_00275B44 itself. D_00275B44 = S = 0x1A00000, which holds a different
    position quadword every 16 bytes. The packets go to S's memory, and the
    end tag's cursor store moves D_00275B44 before it is read. The moved
    context's +0xA0 quadword is read at 0x275BC0 (the low four bits
    dropped); it is set to x = 1e9, z = 1000, w = 1, so the key follows the
    point.
  - `table` (001F4A10): D_0028A56C is read after 001C7900. The context is
    copied to 0x28A550, so its cursor word is D_0028A56C. 001C7900's cursor
    store moves the table pointer to the cursor after its packet, where a
    copy of the 0x30000 bytes of the 001C6120 table is placed. 001C6120 must
    be called with that address.
- **64 KiB cursor carry** (21 cases: 3 sites on the 7 main beats; quick 9).
  A cursor store whose sum carries into the cursor's upper half. The shared
  tag helper runs at the slot tag of 001D5170 and 001D4FC0, placed 0x10
  below a 64 KiB boundary. 001D4FC0's inline first tag runs 0x80 below one
  (cursor + 0xF0). Each case asserts that the carried cursor was stored.
- **View edges** (21 cases; quick 9). The 001F8350 pairing with the point
  quadword at the first scratchpad bytes (0x70000000), the last ones
  (0x70003FF0) and the last RAM quadword (0x01FFFFF0), all mapped on the
  EE. An access that starts or ends exactly at a view's edge must be
  served by that view.
- **001CD2B0 large n** (21 cases; quick 11). The 001F8350 pairing with
  f12 = f13 ∈ {2.3e6, 2.51e6, 3.03e6}, f14 = 0 and f15 = 2^25 gives a
  001CD180 result n of 25 bits. The EE's CVT.S.W truncates n. A rounding
  conversion differs from it when the dropped bit rounds up, which none of
  the unit cases' extents (at most 400) reaches. The test asserts that
  some case has such an n: 6 do in full mode, 4 in quick mode.
- **001E8B90 rounding** (3 cases). Record 0 of the a01_00 grid gets a
  32 × 32 box, and the other records are dead.
  - `xw_round`: x0 = 1.7 (0x3FD9999A). The EE's truncating x0 + w is
    0x4206CCCC, and a rounding add gives 0x4206CCCD. p.x = 0x4206CCCD is
    outside the original's box.
  - `zd_round`: the same for z0 + d.
  - `subx_round`: x0 = −1.5·2^−21 and p.x = 16 − 2^−20. The EE's truncated
    p.x − x0 and a rounded one differ, which moves the mapped cell's
    float_to_int argument.
- **001E8B90 sub-normal, live and saturating words** (9 cases). The same
  box setup as the rounding cases. The EE compares count a sub-normal as 0;
  a host compare would not:
  - `x_denorm_hi`: x0 = −1, w = 1 (x0 + w = +0), p.x = 0x00000001, inside;
  - `x_denorm_lo`: x0 = +0, p.x = 0x00000001, outside (x ≤ x0);
  - `z_denorm_hi`, `z_denorm_lo`: the same for z;
  - `y_denorm`: y0 = −11 (11 + y0 = +0), p.y = 0x80000001, outside;
  - `active_neg`, `active_high`: +0x54 = 0x80000000 or 0x00010000; any
    nonzero word is live;
  - `w_nan`: w = 0x7FC00000, with p = (1, 0, 5.5). The EE treats the
    pattern as a number: x0 + w saturates to MAX, so p is inside, and
    px = 32·(p.x − x0) / w is finite (0x7F7FFFFF reaches 001281C0). A host
    divide would give a NaN;
  - `dz_big`: d = 2^125 and p.z = 1.5·2^124. 32·(p.z − z0) passes MAX, and
    the EE saturates it where a host multiply gives infinity.
- **001E8B90 mixed corners** (48 cases; quick 4). A 32 × 32 box at the
  origin, p at column 0 / row 31 or column 31 / row 0. The cell there
  holds one of 4 values, and f12 takes one of 6 values. The clamped
  neighbours fall on that cell, which gets four adds in the original's
  order (section 1). At a corner where both coordinates clamp the same
  way, both loop orders give the same sequence, so only mixed corners
  test the order.
- **001E9E60 record byte** (5 cases). Record +0x5C = 1 with one of the bytes
  +0x5D, +0x5E or +0x5F also set to 1, and +0x5C = 0x81 or 0x41. The
  routine reads +0x5C as a byte and compares all eight bits with 1. Nothing
  bounds that record byte, so 0x81 is a valid input; a test on its low
  seven bits alone would take 0x81 for 1.
- **Pinned quick seeds.** Quick mode also runs the gs unit seeds 1680, 344
  and 637 and the hud unit seeds 77 and 4494. Each of these full-mode cases
  kills a mutant that no other quick case kills:
  - gs 1680: 001CD180's 0.5 · f12 as a host multiply;
  - gs 344: n − f14 as a host subtract;
  - gs 637: 1 − v as a host subtract;
  - hud 77: 001E8B90's p.x − x0 as a host subtract;
  - hud 4494: 001E9E60's MADD split into a multiply and an add.
- **Packet +8** (21 cases; quick 8). 001E9E60 with the packet cursor that
  001CB5F0 reads (context +0x18) moved by 8, so every packet starts 8 bytes
  off a quadword. Its quadword stores (the blend quadword and the
  00102948 / 00102958 copies) then drop the low four bits, and its word and
  doubleword stores stay aligned. The case asserts that 001CB5F0 returned a
  pointer 8 bytes off.
  - "A quadword access ignores the low four bits" is covered bit by bit:
    - stores: bit 3 here; bit 2 by the pinned `stq4` case (below);
    - loads: bits 2 and 3 by the gs unit cases (001CD070's p at +4, +8,
      +0xC); bits 0 and 1 by the pinned `ldq_pair` and `ldq_f4a10` cases
      (p and the subject pointer at +1, +2, +3).
  - Bits 0 and 1 of a quadword store address are never set in an execution
    the EE completes, so they are not tested:
    - 001CD070's and 001CD180's own quadword stores go to the constants
      0x70003600 and 0x70003610;
    - every other quadword store is in 001E9E60, at a multiple of 16 past
      a packet start blk. The packet's first store is the word store to
      blk, which is an address error unless blk is 4-aligned.
  - The earlier claim that packets +4 and +0xC off are not EE executions
    was wrong. With every packet 4 off, the 9-quadword packet's doubleword
    stores are misaligned. But the `stq4` case shows an earlier packet's
    copy moving the cursor, so that the last packet is aligned and the run
    completes. +0xC is not run, because bits 2 and 3 are each covered.
- **Scratchpad packet** (21 cases; quick 9). An EE packet cursor may point
  into the scratchpad. The cursor is placed so that 001E9E60's 5-quadword
  packet starts at 0x70003AC0, 0x70003AD0 or 0x70003AE0. Its 00102958 copy
  of the four quadwords at 0x70003AC0 then writes over quadwords of its own
  source. The original loads all four before the first store; the case
  asserts that the packet landed there.
- **Misaligned quadword arguments.** Some gs unit cases pass 001CD070's p
  at +4, +8 or +0xC from its quadword, and some 001D5A70 cases pass a1 so.
- **Sweep-3 pinned cases** (91 cases: 13 shapes on the 7 main beats; quick
  19: the 13 on a01_00, the three `ldq_pair` on a01_01 and the three
  `ldq_f4a10` on a01_04). Each is an execution the EE completes, and each
  asserts that it reached the state it is built for.
  - `ldq_pair` (p = 0x700036D0 + 1, 2 or 3): the 001F8350 pairing with the
    point quadword at 0x700036D0. A load that kept bit 0 changes what
    reaches the workers on a01_01, a01_04, a01_05 and a01_06; one that kept
    bit 1 does so on a01_00, a01_04, a01_05 and a01_06.
  - `ldq_f4a10` (+1, +2, +3): 001F4A10 with D_00275B44 moved by that much.
    The subject pointer is used only by the quadword load of its +0xB0
    point.
  - `stq4`: 001E9E60 with the context copied to X = 0x1A00000 and the
    packet cursor placed so that the six GIF packets and the 5-quadword
    packet start 4 bytes past a quadword, the latter at X − 0x2C. That
    packet's 00102958 copy of 0x70003AC0.. stores its last quadword at
    X + 0x10 (X + 0x14 with the low four bits dropped). Its third word is
    Y = 0x1B00000, so the cursor at X + 0x18 becomes Y and the 9-quadword
    packet starts at Y + 0x120, aligned for its doubleword stores. The
    case asserts all eight packet addresses.
  - `rand_x`: 001F4A10 with D_0024295C = v − 0x58 (v = 0x700038B0), so
    00122BB8's LCG store lands on v + 0, the x word, which the original
    reads after that call. The start state is chosen so that the new state
    is 0x80. The case asserts the call and the stored state.
  - `t_alias`: 001E9E60 with the 9-quadword packet at node + 0x70, so its
    +0x10 word copy (record +0x48) lands on a0 + 0x80, the blend t, before
    the original reads t. The case asserts the packet address and the
    changed word.
  - `cd180_x` (f12 = −6 or −600) and `cd180_y` (f13 = 1e30 or 3e38): direct
    001CD180 with the centre word at 0x7FFFFFF0 and the other at 0x8000.
    x − hw then wraps in 32 bits and passes the first bound, and y + hh
    wraps negative and fails the last one. The case finds the extents as
    the bound cases do and asserts that the wrap happens.
- **001E9E60 record words.** Half of its unit cases also perturb the record
  words the 9-quadword packet computes from (+0x10..+0x1C, +0x38..+0x50).
- **frame.** 0015B610 over every combination of b ∈ 0..5, +5 ∈ 0..5 and
  check result ∈ {0, 1, −1}, plus three edge values, plus the three
  state-setting paths (b = 1, check 0, +5 ∈ {3, 0, 1}) with the player's
  bytes +7 = 0x5A and +0x1F1 = 0xA5. The byte stores at +6 and +0x1F0 must
  leave those bytes alone (114 cases).
- **faults** (native only), at every one of the 16 entries with its real
  argument shape:
  - each worker the entry can reach, unbound in turn. The fault must name
    that worker, and nothing may be written or called.
  - a latched fault;
  - no views, and views whose bytes are NULL. The entry must fault with
    code 4 and record the first data address the ORIGINAL routine accesses
    in an oracle run from the same state. That address is found by
    watching the interpreter's loads and stores, instruction fetches and
    the private stack excluded. For example: 0x28A5A4 for 001D5BD0,
    0x275670 for 001D4FC0, 0x275C04 for 001CD180, 0x700038BC for 001F4A10,
    0x810700 for 001E8B90, 0x275C34 for the handlers and 0x70003B8D for
    0015B610. 001F4CC0 calls a worker before it touches memory, and it
    runs.
  - two accesses whose end would pass 2^32: 001CD070 with p = 0xFFFFFFF0
    and 001F4A10 with v = 0xFFFFFFF0 (its first read is v + 0xC =
    0xFFFFFFFC). No view covers them, so they fault with that address. On
    the EE, 0xFFFF8000.. is mapped kernel memory outside the model, so
    these run on the native side only.
  - 001E8B90 with the RAM view ending at 0x810700, its first data address:
    code 4, detail 0x810700, function 001E8B90, no call. A bound that let
    one byte past a view through would read host memory there instead.
  - 001CD2B0 with the RAM view starting at 0x275C04. The view covers the
    D_00275C04 word that the inline 001CD180 body reads, but not
    D_00275C00. The fault must be code 4, detail 0x275C00, function
    001CD2B0.
  - a failing worker at every entry that has workers (13 entries). Two of
    them make no worker call from a01_00's state, so they run from another
    state: 001D5BD0 on a01_04, whose records build chains (48 calls), and
    001E8B90 with the player inside a live 32 × 32 record box (2 calls).
    Before this round the group ran 001D5BD0 on a01_00, where it makes no
    worker call, so that entry's failing-worker check tested nothing.
    Quick mode fails the first call of each distinct worker plus the
    first, second, middle and last call; full mode fails every call. The
    entry must return −1 with code 2 naming that worker, and make no
    further call.

  The group runs in a forked child process. A module that skipped its
  NULL-worker check would call through a NULL pointer, and a wrong view
  bound would dereference a wild pointer. Either kills the process, and
  the child turns that into an ordinary test failure naming the last
  entry and worker it started.
- **Branch coverage.** Every conditional branch of the 16 routines must be
  seen taken and not taken (58 branches). Two outcomes are impossible by
  construction and are asserted never to occur. They are 001E8B90's
  "mapped cell below 0" tests at 001E8CE8 and 001E8D08. The box test admits
  x > x0 and x ≤ x0 + w, which forces w > 0 and x − x0 ≥ 0, so the index
  cannot be negative.

Results (2026-09-26, close-out round):

```
$ python3 tools/test_area01_render_reference.py
mode quick: 201 capture cases over 12 AREA01 beats, 60 of 4,000 vif unit cases, 123 of 8,000 gs
unit cases, 36 001CD2B0 t = 0 boundary cases, 24 of 112 001CD180 bound cases, 24 of 126 direct
001D5A70 cases (results 0, 1, 0xFF), 33 of 189 context +0xA0 cases, 16 of 56 tag-alias cases, 19 of
91 D_00275670-alias cases, 9 of 21 other re-read alias cases, 7 re-read cases of the sweep-2 review
(12 of 42 items; 5 count items skipped: record 0 builds no chain on that beat), 9 of 21 64 KiB
cursor-carry cases, 9 of 21 view-edge cases, 11 of 21 001CD2B0 large-n cases (4 where n's
truncation and rounding differ), 3 001E8B90 rounding cases, 9 001E8B90 sub-normal / live-word /
saturation cases, 4 of 48 001E8B90 mixed-corner cases, 5 001E9E60 record-byte cases, 8 of 21 packet
+8 cases, 9 of 21 scratchpad-packet cases, 19 of 91 sweep-3 pinned cases (odd quadword loads,
packets 4 off, rand x, blend t, 001CD180 wraps), 142 of 8,000 hud unit cases, 114 0015B610 cases,
900 cases in all (0 unmeasured on both sides; 1862 packet-builder calls; RAM + scratchpad + worker
log byte-exact at the end), 4132 worker entries with RAM + scratchpad + arguments equal to the
original's at entry, 58 conditional branches both ways (2 outcomes impossible by construction), 143
fault cases (EM_TEST_FULL=1 runs the exhaustive sweep)
area01 render reference: PASS

$ EM_TEST_FULL=1 python3 tools/test_area01_render_reference.py
mode full: 201 capture cases over 12 AREA01 beats, 4,000 vif unit cases, 8,000 gs unit cases, 36
001CD2B0 t = 0 boundary cases, 112 001CD180 bound cases, 126 direct 001D5A70 cases (results 0, 1,
0xFF), 189 context +0xA0 cases, 56 tag-alias cases, 91 D_00275670-alias cases, 21 other re-read
alias cases, 37 re-read cases of the sweep-2 review (42 of 42 items; 5 count items skipped: record
0 builds no chain on that beat), 21 64 KiB cursor-carry cases, 21 view-edge cases, 21 001CD2B0
large-n cases (6 where n's truncation and rounding differ), 3 001E8B90 rounding cases, 9 001E8B90
sub-normal / live-word / saturation cases, 48 001E8B90 mixed-corner cases, 5 001E9E60 record-byte
cases, 21 packet +8 cases, 21 scratchpad-packet cases, 91 sweep-3 pinned cases (odd quadword loads,
packets 4 off, rand x, blend t, 001CD180 wraps), 8,000 hud unit cases, 114 0015B610 cases, 21250
cases in all (0 unmeasured on both sides; 33532 packet-builder calls; RAM + scratchpad + worker log
byte-exact at the end), 91299 worker entries with RAM + scratchpad + arguments equal to the
original's at entry, 58 conditional branches both ways (2 outcomes impossible by construction), 191
fault cases
area01 render reference: PASS
```

(The six 001E8B90 boundary cases are counted in the totals but not named
in the line.)

Measured times (`/usr/bin/time -p`; user and sys include the worker
processes). The machine is shared with other lanes; load averages are on 10
cores.

| run | load average | wall | user | sys |
|---|---|---|---|---|
| quick, cold (both libraries deleted first; includes their build) | ~126 | 6.3 s | 7.1 s | 0.7 s |
| quick, warm | ~126 | 5.9 s | 7.1 s | 0.7 s |
| quick, serial (`EM_TEST_JOBS=1`) | ~130 | 11.6 s | 6.9 s | 0.6 s |
| quick with `EM_A01R_FULLCMP=1` (self-check) | ~127 | 25.6 s | 49.8 s | 1.1 s |
| full (`EM_TEST_FULL=1`) | ~126 | 57.3 s | 115.3 s | 3.3 s |

The default (quick) run is about 7.8 s of CPU (user + sys), inside the
~10 s target, with every pinned kill case in it. The close-out round added
23 quick cases and 95 full cases, and 45 quick and 86 full fault cases (the
failing-worker check now runs at all 13 entries with workers). Round 5:
quick 6.7 s user, 0.7 s sys; full 112.3 s user, 3.0 s sys. Round 4: quick
5.0 s user, 0.8 s sys; full 105.2 s user, 4.8 s sys. Round 3: quick 4.5 s
user, 0.6 s sys; full 100.1 s user, 3.8 s sys. Round 2 took 23.5 s to
7 min 6 s wall for the quick mode (user ~28 s, sys 12 to 180 s), and 7 min
32 s for the full mode (user 601 s, sys 277 s).

Plumbing change this round (test only): the process pool's teardown after
a failure is bounded (`close_pool`). In the mutation rerun below, one
failing run (`f_case4_440`) hung for 16 minutes in `Pool.terminate()`
after its assertion, with its workers already exited, so the test never
reported. The teardown now runs in a daemon thread and kills the workers
if it has not finished within 30 s. Rerun on the final file, that mutant
is killed in 5.4 s. No case or check changed.

**Mutation evidence.** Each mutant copies the lane sources, makes one
change, builds a private library and runs this test's `main()` in quick
mode. Mutants run one at a time, each test with at most four workers.
Driver: `build/area01/render/r6/mutdb.py` (every set below, read from its
review's own table), `run_mutant.py` and `run.sh`.

350 single-operation mutants were tried across the rounds, plus 4 no-op
controls:
- round 3's 166: the lane's 109, the round-2 review's 56 (52 mutants of the
  translation and 4 planted-store probes), and `P_far_overwrite`;
- the sweep-1 review's 63 (`N_*`) and `r4_vif_4fc0_inline_cached_cur`
  (round 4);
- the sweep-2 review's 59 (`N2_*`);
- the sweep-3 review's 44 (`M3_*`);
- the final review's 16 (`F01`..`F16`);
- one close-out check, `R6_ldq_keep_bit1` (a quadword load that keeps
  address bit 1), which verifies the new odd-load cases' claim.

The controls are `CONTROL_identity`, `CONTROL2_identity`,
`CONTROL3_identity` and `CONTROLF_identity`. Some mutants repeat others
under another name.

**310 are killed and 40 are proven equivalent** (the table below). No named
survivor remains. The sweeps did not converge: each review's fresh
mutants found new survivors. So this covers these 350 mutants only. It is
not a claim that the test kills every wrong translation.

All 354 runs were redone in quick mode on the final test file:
- the whole set: 34 min 3 s wall, user 1160 s, sys 179 s, load ~100 to
  130. It gave 309 kills, 40 equivalent survivors and the four controls
  surviving. `f_case4_440` hung (above), and after the teardown fix it is
  killed;
- then the 55 named mutants below plus `CONTROL3_identity`, on the file with
  that fix: all 55 killed and the control survives (3 min 32 s wall, user
  258 s, sys 31 s).
- 226 of the 310 kills come from the entry check: 109 on RAM or
  scratchpad, 117 on the arguments or the call count. 23 come from the fault
  group.
- The 40 equivalent survivors were run in full mode in earlier rounds (the
  36 in rounds 4 and 5, the four `M3_*` in sweep 3), and all survived.
  None of them can be killed, so that was a consistency check only; it was
  not redone.

Named survivors, each rerun on the final file and killed in quick mode:

| mutant | killed by (quick) | where |
|---|---|---|
| `M3_ldq_keep_bit0` (quadword load keeps bit 0) | pinned `ldq_pair` +1, a01_01 | entry check of call 0: an extra 001281C0 call |
| `R6_ldq_keep_bit1` (keeps bit 1; close-out check) | pinned `ldq_pair` +2, a01_00 | entry check of call 0: an extra call |
| `M3_stq_keep_bit2` (quadword store keeps bit 2) | pinned `stq4`, a01_00 | entry check of call 1 (001CB5F0): RAM at 0x19FF560 |
| `M3_view_end_plus1` (one byte past a view accepted) | faults, view ending at 0x810700 | detail 0x82CD54 against 0x810700 |
| `M3_2b0_function_drop` (001CD2B0 does not reset the function after 001CD180) | faults, view starting at 0x275C04 | function 001CD180 against 001CD2B0 |
| `M3_180_nowrap_9001` (x − hw compared without the 32-bit wrap) | pinned `cd180_x` −6, a01_00 | result 0 against 9 |
| `M3_180_nowrap_7900` (y + hh without the wrap; full mode only before) | pinned `cd180_y` 1e30, a01_00 | result 0x10000000 against 0 |
| `M3_f4a10_x_early` (v +0 loaded before 00122BB8) | pinned `rand_x`, a01_00 | entry check of call 1 (001C7900): dir.x |
| `M3_8b90_host_div_px` (host divide for px) | box `w_nan` | entry check of call 1 (001281C0): 0x7FC00000 against 0x7F7FFFFF |
| `M3_8b90_host_mul_dz` (host multiply for dz) | box `dz_big` | entry check of call 0 (001281C0): 0x7F7FFFFF against 0x40FFFFFF |
| `M3_9e60_t_early` (t loaded before the packet's word copies) | pinned `t_alias`, a01_00 | entry check of call 8 (001CB950): RAM at 0x7B2180 |
| `F14_9e60_byte_mask7F` (record +0x5C tested on its low 7 bits) | record byte +0x5C = 0x81 | entry check of call 8 (001CB950): the word pair |

Also rerun and killed in quick mode on the final file:
- the sweep-1 review's twelve non-equivalent survivors (`N_view_end_lt`,
  `N_view_start_gt`, `N_tail_drop_end_reload`, `N_tail_ctx_from_body`,
  `N_2b0_host_cvt`, `N_f4a10_packet_late`, `N_8b90_grid_cached`,
  `N_8b90_host_xw`, `N_9e60_byte_ld32`, `N_9e60_cached_ctx2`,
  `N_9e60_top_early`, `N_f4a10_drop_ctx_reload2`) and its five
  full-mode-only kills (`N_180_host_half_w`, `N_2b0_host_sub`,
  `N_2b0_host_one_minus`, `N_8b90_host_sub_x`, `N_9e60_madd_split`,
  pinned by the gs seeds 1680, 344, 637 and the hud seeds 77, 4494);
- the sweep-2 review's eighteen non-equivalent survivors, plus
  `N2_8b90_loop_swap` and `N2_view_end_wrap32` (the table below);
- the round-3 task's five: `vif_tail_cached_ctx`, `gs_f4a10_cached_ctx`,
  `m_stq_mask7`, `r3_mem_tag_drop_reread_addr` and
  `r3_vif_4fc0_inline_cached_addr`;
- `f_case4_440` (the hang above).

Where the sweep-2 review's survivors are killed (unchanged from round 5):

  | mutant | killed by | where |
  |---|---|---|
  | `N2_tag3_final_st16` | re-read `recword` (a01_00; its chunk loop's cursor store carries past 64 KiB), and the cursor-carry group | entry check of call 3 (001CAAC0): RAM at 0x811CDE |
  | `N2_4fc0_final_st16` | cursor carry `first` (a01_00) | entry check of call 0 (00121870): RAM at 0x811CDE |
  | `N2_proj_host_min` | context +0xA0 `x_nan` through 001F4A10 (a01_00) | entry check of call 4 (001281C0): the key |
  | `N2_5bd0_count_reread` | re-read `count` (a01_04) | 4 worker calls against 48 |
  | `N2_tail_recword_early` | re-read `recword` (a01_00) | entry check of call 3 (001CAAC0): its arguments |
  | `N2_tail_v_before_endtag` | re-read `endtag_v` (a01_00, 001D5170) | entry check of call 3 (001CAAC0): vector x |
  | `N2_2b0_clamp_ulp` | boundary `v_over1` (a01_01, n = 51) | result 0xB4000000 against 0 |
  | `N2_f4a10_subject_early` | re-read `subject` (a01_00) | entry check of call 4 (001281C0): the key |
  | `N2_f4a10_table_early` | re-read `table` (a01_00) | entry check of call 2 (001C6120): table 0x564DC0 against 0x564EC0 |
  | `N2_8b90_host_le_z` | box `z_denorm_hi` | 0 worker calls against 2 |
  | `N2_8b90_active_signed` | box `active_neg` | 0 worker calls against 2 |
  | `N2_9e60_ctx3_cached` | D_00275670 alias `copy3` (a01_00) | entry check of call 8 (001CB950): RAM at 0x275682 |
  | `N2_9e60_ctx1_early` | D_00275670 alias `word48` (a01_00) | entry check of call 8 (001CB950): RAM at 0x2756C2 |
  | `N2_frame_st16_6` | frame, +7 = 0x5A (+5 = 3) | entry check of call 1 (00182D70): RAM at 0x8102B7 |
  | `N2_frame_st16_1f0` | frame, +0x1F1 = 0xA5 (+5 = 3) | entry check of call 1 (00182D70): RAM at 0x8104A1 |
  | `N2_fault_detail0` | faults, no views (001D5BD0) | detail 0 against 0x28A5A4 |
  | `N2_view_nullbytes` | faults, NULL view bytes (001D5BD0) | no BAD_ADDRESS fault |
  | `N2_need_4fc0_no1f80` | faults, 001D4FC0 entered with 001D1F80 unbound | the mutant calls the NULL worker; the fault group's child dies with signal 11, which the test reports as a failure naming the last entry started (001D4FC0, `w_001D1F80` unbound) |

The planted-store probes are each a store that a later write erases, so
the end state is identical. The entry check kills all eight:
`vif_4fc0_erased_store`, `gs_070_early_store`, `hud_random_store_late` (the
lane's), `P_vif_copy_overwrite`, `P_vif_4750_overwrite`,
`P_frame_1f0_overwrite`, `P_gs_c7900_overwrite` (the round-2 review's) and
`P_far_overwrite`. In round 5, with only the entry check's RAM and
scratchpad compare disabled (`noentry`), seven of them survived, so for
those seven the page compare is what kills them. The eighth,
`P_gs_c7900_overwrite`, is also caught by the end check of the `entry` case.
The `noentry` variants were not redone this round.

**The 40 equivalent survivors.** "Equivalent" here means the lane and the
mutant give the same return value, worker calls (arguments included), RAM
and scratchpad for every input the original can receive. That holds in
every execution the EE completes, with views covering the memory the
execution touches, and with each worker behaving as the original callee.
The fault record is outside these proofs in two cases: executions the EE
cannot complete, and worlds whose views leave out memory the execution
touches (section 1 lists what the test checks of it). For example,
`M3_4fc0_swap_addr_qwc` changes which of two uncovered addresses is
recorded first when both lie outside every view.

The exhaustive checks are in scratch programs, all built against the
port's `em_ee_float.h`, and all print counts only:
- `build/area01/render/r4/exhaust.c` (log `exhaust.log`) for the first 22;
- `build/area01/render/r5/exhaust2.c` (log `exhaust2.log`) for the `N2_*`
  rows. It is a copy of the sweep-2 review's program, rerun in round 5 with
  the same counts (70.5 s wall, user 44.5 s, one process);
- `build/area01/sweep_RENDER_3/addsym.c` (the sweep-3 review's), cited but
  not load-bearing for `M3_f4a10_add_swap`.

| mutant | why it is equivalent |
|---|---|
| `vif_5a70_w1`: 001D5A70 point w = 0 instead of 1.0 | The point's w lane is never read. The original's fourth product step broadcasts the w lane of vf0 (its operand register is vf0), not of the point. `em_a01r_transform` likewise reads `EM_A01R_VF0[3]`. |
| `gs_2b0_clamp_lt`: 001CD2B0 clamp `c_le(v, 1)` → `c_lt(v, 1)` | They differ only when v equals 1.0 exactly, and then both leave v = 1.0. |
| `gs_2b0_drop_t_store`: 001CD2B0's store of t to 0x70003680 dropped | The routine's next memory access is the final store to 0x70003680, which overwrites it. In between there is only arithmetic: no load, no store and no worker call. |
| `mem_clip_no_daz_w` and `N_clip_no_daz_x`: the clip test without DAZ on w, or on x/y/z | The clip test's input is always the result of `em_a01r_transform`'s last step, whose VU form clamps its operands. `em_eei_pack` flushes a sub-normal magnitude to a signed zero, so no lane is ever sub-normal and DAZ never changes one. |
| `r3_mem_clip_no_exp255`: the clip test's exponent-255 guard never true | The guard is unreachable. The last multiply-add step saturates fs, ft and ACC to finite values (`em_eei_saturate`), and `em_eei_pack` returns ±MAX in place of an exponent of 255. |
| `r3_hud_e9e60_madd_swap`: 001E9E60 blend, the two multiplicands of the MADD swapped | `em_eei_ee_raw_product` (DAZ, NaN/Inf rules, then `em_eei_exact_product`) is symmetric in its two operands. |
| `g180_hwlt_le`: 001CD180 `hw < hh` → `hw <= hh` | They differ only when hw = hh. The store then writes hh to 0x70003610, whose word was just loaded as hw = hh, so memory is unchanged. |
| `m_cd370_shl5`: 001CD370(i) with i << 5 instead of i << 6 | Both call sites (001CD070 in gs.c and 001D5A70 in vif.c) pass the constant 0, as the original's instructions do, and 0 << 5 = 0 << 6. |
| `r3_hud_random_cached_work`: D_00275C34 not reloaded after the +4 seed store | The seed store changes D_00275C34 only if work + 4 = 0x275C34 (a word store must be aligned), that is work = 0x275C30. The original then reloads 0x275C30, stores 0x275C30 · 37 + 11 = 0x05B052FB there, and next loads a word from 0x05B052FB + 0x54. That load is misaligned, an EE address error, so the EE cannot complete that execution. |
| `r3_vif_4fc0_inline_cached_qwc`: 001D4FC0's inline tag reuses the cursor loaded before the addr store for the qwc store | They differ only when the addr store (a word store of 0 at cursor + 4) lands on the cursor word, which forces cursor = context +0x18. The cursor word is then 0, so the original's reloaded cursor is 0 and its qwc store goes to address 0. No TLB entry maps 0x00000000 (above), so the EE raises a TLB exception there and cannot complete that execution. |
| `r4_vif_4fc0_inline_cached_cur`: 001D4FC0's inline tag stores cursor + 0xF0 from the cursor loaded before the qwc store | The two differ only when the qwc halfword store (0xE at the cursor) lands on the cursor word itself, at context +0x1C. The cursor it stores through is the one loaded after the addr store, so it must be aligned to 4 (the addr word store at +4 is otherwise an address error) and it must not have been changed by that store (a hit would make it 0, and the qwc store would go to unmapped address 0). So the cursor equals context +0x1C after the id byte store, and before it too, unless the id byte (0x10) changed it. Unchanged means context +0x1C already has 0x10 as its top byte: the context lies at 0x0FFFFFE4.. 0x10FFFFE3, which is unmapped apart from the EE register page (outside the model, section 5). Changed means the id byte replaced the low byte of a cursor at context +0x19 and gave context +0x1C: context = 0xF4 (mod 0x100). Then the qwc store leaves the cursor word's low half 0x000E, and the original stores cursor + 0xF0 = 0xFE (mod 0x100) back. 00121870's copy of 0xE0 bytes goes to the reloaded cursor + 0x10, which is 0x1E..0xFE (mod 0x10000) above a 64 KiB boundary. Context +0x1C is 0x10 (mod 0x100), so the copy covers neither the cursor word nor D_00275670 (0x5670 above its boundary). 001D2090 (`vif_append_ref_tag`, decomp C without a branch) then stores its first tag's addr word at cursor + 4, which is 2 (mod 4): an address error. The oracle confirms this at contexts 0x1A000F4, 0x19FFFF4 and 0x1A002F4 (scratch `probe_inline_cur.py`): 00121870 completes, and 001D2090 faults at 0x1A00102. So the EE cannot complete any execution where the two differ. |
| `N_proj_max_negzero`: the projection's max(w, +0) → max(w, −0) | The clamped w lane is used only as `em_vu_ftoi4_bits` of it (screen[3]); clip is copied before the clamps. Exhaustive check 2: ftoi4(max(x, +0)) = ftoi4(max(x, −0)) for all 2^32 x. |
| `N_5bd0_r_bit0`: 001D5BD0 `r == 0` → `(r & 1) == 0` | r is 001D5A70's result, which is 0, 1 or 0xFF (both in the original and in `body_001D5A70`). For those three values the two tests agree. |
| `N_070_return_cached`: 001CD070 returns the z word it stored instead of reloading 0x70003608 | Between the quadword store to 0x70003600 and the reload come only the 001281C0 call and a word store to D_00275C04 (RAM). 001281C0 (float_to_int) stores only into its own 48-byte stack frame: two stores off sp. Its one callee, 001278C0, gets a1 = that sp and makes nine word stores off a1 at offsets 0..12. A scan of both functions' instruction words, reported as counts only, shows no other store. With the stack in main RAM (section 5), nothing writes 0x70003608..0B, so the reload returns the stored word. |
| `N_2b0_signbit_lt`: 001CD2B0 `c_lt(t, 0)` → t's sign bit | Exhaustive check 3: the two differ exactly for t in 0x80000000..0x807FFFFF (−0 and the negative sub-normals). t = cvt(n) − f14 with n ≠ 0 is never one of them. cvt(n) is normal with \|cvt(n)\| ≥ 1 (exhaustive check 5). If f14 equals cvt(n), the model's difference is not −0 (check 6 with k = 0, every n). If \|f14\| < 0.5 (DAZ included), \|t\| > 0.5. Otherwise both operands are multiples of 2^−24, so a nonzero difference is at least 2^−24 in magnitude, and the truncated result is normal with the exact result's sign. An exponent-255 f14 gives a huge result. Checks 6 (f14 one to four units from cvt(n), every n) and 7 (10^9 random pairs) find no such t. |
| `N_2b0_host_le`: 001CD2B0 clamp `c_le(v, 1)` → a host `v <= 1.0` | v is +0 or `em_ee_div_bits(t, f15)`. Exhaustive check 4: the two compares differ only on the 2^23 − 1 negative exponent-255 patterns with a nonzero mantissa (a host NaN). `em_ee_div_bits` never returns an exponent of 255: its special cases return ±MAX or ±0, and every other result goes through `em_eei_pack`, which saturates. |
| `N_9e60_a1_u16`: 001E9E60 multiplies (uint16_t)a1 | The boot ELF has one call to 001E9E60, in 0015A2C0 at 0x0015A4B8. No word in the boot ELF or the overlays holds its address, and no overlay calls it. That call loads a1 with a zero-extending halfword load in its delay slot, so a1 is in 0..0xFFFF and (uint16_t)a1 = a1. |
| `N_rand_host_cvt` / `N_rand_host_div`: 001EB020 / 001EC270's (r >> 16 & 0xFFFF) converted, or divided by 65535, on the host | The operand k is in 0..65535. Exhaustive check 1: the EE and the host agree on both the conversion and the quotient for all 65,536 values. |
| `N_frame_busy_byte`: 0015B610 tests `(busy & 0xFF) == 0` | busy is 00182B30's result. The decomp's `src/func_00182B30.c` compiles to the original's 184 bytes exactly (its .text equals the expected object's, which has 8 more zero bytes of padding). It returns only the constants 0 and 1, for which the two tests agree. |
| `N_frame_state_hoist`: 0015B610 loads a +5 before the 00182B30 call instead of after it | The only stores in 00182B30 and its callee 0021BB00 are 00182B30's two quadword register saves into its own 32-byte stack frame. 0021BB00 has no store instruction, per the same counts-only scan. The frame lies below 0015B610's stack pointer, in the thread stack (section 5), where no entity record lives. So a +5 is the same before and after the call. |
| `N2_clip_w_unchecked`: the clip test's exponent-255 guard skips the w lane | The guard is never true for any lane (`r3_mem_clip_no_exp255` above). The clip test's two callers, 001D5A70's point test and 001CD070, pass it the output of `em_a01r_transform`, whose last step saturates and whose `em_eei_pack` never produces an exponent of 255. |
| `N2_clip_sign_raw`: a clip flag's sign taken from the raw lane instead of the DAZ-applied one | `em_eei_daz` keeps the sign bit: a sub-normal becomes a zero of the same sign. So the two sign bits are equal for every lane value. |
| `N2_fault_overwrite`: a later fault overwrites the latched one | `em_a01r_fault` is never reached while a fault is latched. All 16 entries start with `need()`, which returns −1 without calling it when a fault is latched. Every other caller returns −1 at once, and every result that can carry a fault is checked (`TRY`, `CALLW`, the `if (!p) return -1` after `em_a01r_mem`). A scan of the four .c files and mem.h for helper calls outside those forms finds none. So after the first fault each routine returns −1 with no further access, worker call or fault. The workers get their own context, not the module state. |
| `N2_tail_chunk_le`: chain_tail's chunk = n ≤ 0x1F8 ? n : 0x1F8 | The two differ only at n = 0x1F8, where both give 0x1F8. |
| `N2_clamp_le0`: 001E8B90's clamp gives 0 for i ≤ 0 | The two differ only at i = 0, where both give 0. |
| `N2_5a70_mask7F`, `N2_070_nomask`: the clip flags masked with 0x7F, or not masked, instead of 0x3F | `em_a01r_clipw` sets only bits 0..5 (1 or 2 shifted by 2k for k < 3), so neither mask changes anything. |
| `N2_rand_nomask`: 001EB020 / 001EC270 convert r >> 16 without the & 0xFFFF | r is a uint32_t, so r >> 16 is at most 0xFFFF already. |
| `N2_2b0_lt_negzero`: 001CD2B0 tests c_lt(t, −0) instead of c_lt(t, +0) | Exhaustive: the two agree for all 2^32 t (scratch `build/area01/render/r5/exhaust2.c`, check 2: 0 differ). |
| `N2_2b0_v_negzero`: 001CD2B0's v starts at −0 instead of +0 | v keeps its initial value only when t < 0. Then c_le(v, 1) holds for both +0 and −0, so the clamp does not change it. The result is 1 − v, and 1 − (+0) = 1 − (−0) = 0x3F800000 (exhaust2 prints all four values). v is used nowhere else. |
| `N2_f4a10_div2p31_as_mul`: 001F4A10's cvt(r) / 2^31 computed as cvt(r) · 2^−31 | Exhaustive: the two agree for all 2^32 r (exhaust2 check 1: 0 differ; no nonzero quotient is sub-normal). |
| `N2_f4a10_host_div32`: 001F4A10's f2 / 32 on the host | The EE's and the host's f2 / 32 agree on every f2 in {±0} ∪ [2^−38, 2^26). Exhaust2 check 3 tests all 1,073,741,826 such patterns and finds 0 that differ; over all 2^32 patterns, 117,440,478 differ. Every f2 001F4A10 can compute lies in that set. base = cvt(w3) / 128 is ±0 or has \|base\| in [2^−7, 2^24], since w3 is an int32 and \|cvt(w3)\| is in [1, 2^31]. q = cvt(r) / 2^31 has \|q\| ≤ 1. Either q = −1 exactly (r = 0x80000000), or \|q\| ≤ 1 − 2^−24, because cvt truncates, so \|cvt(r)\| ≤ 2^31 − 128 for any other r. m = q · base is truncated, so \|m\| ≤ \|q\|·\|base\|. f2 = base + m. If base = ±0, f2 = ±0. If q = −1, m = −base exactly and f2 = +0. If m has base's sign, \|base\| ≤ \|f2\| ≤ 2\|base\| ≤ 2^25. Otherwise the add's pre-trim only shrinks \|m\| (`em_eei_trim`), so the exact difference is at least \|base\|·2^−24 ≥ 2^−31. The truncating pack keeps it at or above that value, because 2^−31 is itself a float. So f2 is ±0 or \|f2\| is in [2^−31, 2^25]. (The sweep-2 review's sampling program, `f2range.c`, found no f2 outside the set in 668,435,780 samples; the argument above does not rest on it.) |
| `N2_rand_f12_early`: 001EB020 / 001EC270 load work +0x54 before the seed store and the D_00275C34 reload | The seed store is a word store at work + 4, and it must be aligned on the EE. It cannot change work +0x54, a different word. It changes D_00275C34, and so the reloaded work, only if work + 4 = 0x275C34. Then work = 0x275C30 and the seed read from work + 4 is 0x275C30 itself. The original stores 0x275C30 · 37 + 11 = 0x05B052FB, reloads that as work, and loads 0x05B052FB + 0x54 = 0x05B0534F, a misaligned word load (address error). So the two differ only in executions the EE cannot complete (the same case as `r3_hud_random_cached_work`). |
| `N2_frame_st16_4`: 0015B610's +4 byte store widened to a halfword (it also writes +5 = 0) | The next store writes +5 (`five`), with no load or call in between, so memory ends up the same. The halfword access fails where the original's two byte stores succeed only if a + 4 is the last byte of a view and a + 5 lies in no view. The views end at 0x02000000, 0x70004000 and 0x32000000, and no valid TLB entry maps those addresses, so the original's +5 store raises a TLB exception there. |
| `M3_4fc0_swap_addr_qwc`: 001D4FC0's inline tag stores the qwc halfword before the addr word (each after its own cursor reload) | The two stores cover disjoint bytes, [c + 4, c + 8) and [c, c + 2), with c the cursor reloaded after the id byte. So the order matters only when the first store changes the cursor word (context +0x1C) that the second reloads. The addr store is a word store, so in an execution the EE completes c ≡ 0 (mod 4); context +0x1C is 4-aligned too, since the original loads it as a word. The addr store lands on the cursor word only at c = context +0x18. It writes 0 there, and the original's qwc store then goes to address 0, which no TLB entry maps. The qwc store lands on it only at c = context +0x1C. That is the state of the `r4_vif_4fc0_inline_cached_cur` row above, whose execution the EE cannot complete (001D2090's first tag store is misaligned; oracle-confirmed). So in every execution the EE completes neither store changes the cursor, and both orders write the same bytes. |
| `M3_5bd0_r_eq1`: 001D5BD0's `r != 0xFF` → `r == 1` | r is `body_001D5A70`'s result, which it writes only as 0xFF or as 1 or 0 (on a fault it returns −1 without writing, and the TRY returns at once). The `r == 0` branch is tested first, so at the else-if r is 1 or 0xFF, where the two tests agree. The original 001D5A70 likewise returns only 0, 1 or 0xFF (section 1). |
| `M3_f4a10_rand_mask`: 001F4A10 masks 00122BB8's result with 0x7FFFFFFF before converting it | r is 00122BB8's result. The lane's contract is that every worker behaves as the original callee (the test runs the original instructions for it on both sides). The decomp's `src/func_00122BB8.c` (ee-gcc; its .text is byte-identical to the expected object's, 48 bytes) returns only the new state & 0x7fffffff. So r < 2^31 and the mask changes nothing. A worker bound to something that returned bit 31 set would make them differ; that is outside the contract. |
| `M3_f4a10_add_swap`: 001F4A10's `add(base, f2)` → `add(f2, base)` | `em_ee_add_bits(a, b)` is symmetric in its operands, by its code. In `em_eei_ee_sum`, the exponent-255 block is symmetric: the NaN test is an "or", two infinities give MAX with the sign of a ^ b, or with the shared sign, and one infinity gives its own sign. The pre-trim applies to whichever operand has the smaller exponent field. `em_eei_exact_sum` returns −0 for two zeros only when both signs are set, returns the other operand when one is zero, and otherwise orders the operands by exponent. At equal exponents, the same-sign sum and the opposite-sign difference (the larger magnitude's sign, or +0 when equal) come out the same in either order. The sweep-3 review's numeric check (`build/area01/sweep_RENDER_3/addsym.c`: 1,111,490,560 structured and random pairs, 0 differ) agrees; the argument does not rest on it. |

Static scan of D_00275670 (kept for reference; since both cached-context
mutants are now killed, no claim rests on it). Scratch script
`build/area01/render/r3/scan_275670.py`; it reports counts only. It reads
every 32-bit word of the boot ELF's loadable range, all 19
`extract/OVERLAY/AREA*.BIN` files and AREA18's split text, and counts:
- loads and stores with base $gp (0x27D370) whose byte range overlaps
  0x275670..0x275673. The loads are every load major opcode: 0x1A, 0x1B,
  0x1E, 0x20..0x27, 0x31, 0x36 and 0x37. They are the byte, halfword and
  word loads (the unaligned word pair included), the doubleword loads (its
  pair included), the quadword load, the COP1 word load and the VU
  quadword load. The stores are every store major opcode: 0x1F,
  0x28..0x2E, 0x39, 0x3E and 0x3F, the same widths;
- the same loads and stores with any other base and an immediate in
  0x5661..0x5673;
- upper-half immediate loads of 0x27, and add-immediate / or-immediate
  instructions with 0x5670.

Results:
- Boot ELF: 502 gp-relative word loads (opcode 0x23), no gp-relative store, no other-base
  load or store in the window, 654 upper-half loads of 0x27, no 0x5670
  immediate.
- Overlays: 4 COP1 word loads (opcode 0x31) with immediate 0x5666 from a non-gp base, all in
  AREA00.BIN; nothing else, and no upper-half load of 0x27. A 4-byte load at +0x5666
  covers +0x5666..+0x5669, so these cannot touch 0x275670..73 as the low
  half of a 0x27xxxx address.

## 4. Existing translations (verified, not duplicated)

`python3 tools/test_area01_render_existing_reference.py` imports each
module's own original-instruction harness, unmodified. It feeds the harness
AREA01 inputs and builds the libraries under `build/area01/<EM_LANE>/`
(`EM_LANE` defaults to `render`), as the reference test does. Before
this round the directory was fixed at `build/area01/render/`, so a
reviewer's or sweep's run of it rewrote the lane's own scratch libraries.

| addr | port module (owner) | AREA01 check |
|---|---|---|
| 00163D50, 00164220 | em_player_fall.c | state 8 with +6 forced to 4 or 2 (and +7 over 0..4) on each beat's AREA01 player record. Every callee is scripted on both sides, and the harness replays each scripted callee's writes. **Entry check added here:** at every scripted call, before its writes are replayed, the native side's whole state (the 0x320 actor bytes and the five scratch words, all the memory em_player_fall.c can reach) and the call's logged arguments must equal the original's at the same call. The original routines' own stores are audited to fall only in those regions (the private stack aside), which is what makes that comparison complete. The end state and call log must also match. |
| 001647D0 | em_player_hang.c | its run_case with the AREA01 RAM as the image and the AREA01 player record as the captured record. The code and tables are checked against the ELF first. Its harness already compares all 0x320 actor bytes at every callee entry, before the scripted writes are applied (its own snapshot check). |
| 001F0460 | em_effect_original.c | its decal_sweep (every preset and ring start) over AREA01 beats. Its scripted callees here (allocation, random) only return values; none writes memory the native side holds, so no replay can erase a native store, and the harness's end compare covers the routine's stores. |
| 001F4BF0 | em_status_scene_original.c | its glow check with the 0x700038B0 words of all 12 AREA01 captures, plus 00158590's two. Its callees are recorded, not replayed (no writes). |

```
$ python3 tools/test_area01_render_existing_reference.py
mode quick: 60 00163D50 / 00164220 cases on AREA01 player records (248 scripted calls, 248 with actor
+ scratch + arguments equal to the original's at entry, 29 of 30 conditional-branch outcomes of the
two routines seen), 360 001647D0 cases over 3 AREA01 RAM images (~106 from the AREA01 record), 102
001F0460 decal cases over AREA01 beats, 130 001F4BF0 cases (13 0x700038B0 words from AREA01)
area01 render existing translations: PASS

$ EM_TEST_FULL=1 python3 tools/test_area01_render_existing_reference.py
mode full: 120 00163D50 / 00164220 cases (481 scripted calls, 481 with actor + scratch + arguments
equal at entry, 29 of 30 outcomes), 16500 001647D0 cases over 11 AREA01 RAM images (~5022 from the
AREA01 record), 153 001F0460 decal cases, 572 001F4BF0 cases
area01 render existing translations: PASS
```

Times (fix round 3, `/usr/bin/time -p`, user and sys include the worker
processes; this test now uses at most four workers unless `EM_TEST_JOBS`
says otherwise):

| run | load average | wall | user | sys |
|---|---|---|---|---|
| quick, cold (its four libraries deleted first) | ~89 | 3.4 s | 1.9 s | 1.1 s |
| quick, warm | ~87 | 2.7 s | 1.8 s | 1.1 s |
| full (`EM_TEST_FULL=1`) | ~87 | 7.5 s | 13.2 s | 3.0 s |

Rerun in fix round 4 (the test file is unchanged since round 3), load ~140: quick 2.9 s wall,
user 1.5 s, sys 0.9 s; full 11.6 s wall, user 11.8 s, sys 2.6 s. Rerun in fix round 5 (still
unchanged), load ~53: quick 2.3 s wall, user 1.5 s, sys 0.9 s; full 6.3 s wall, user 12.0 s,
sys 2.4 s. Rerun in the close-out round (only the output directory changed: `EM_LANE`), load
~130: quick 2.3 s wall, user 1.4 s, sys 0.8 s; full 6.8 s wall, user 12.4 s, sys 2.4 s. With
`EM_LANE=render/r6/existing` it passes and builds all four libraries under that directory,
leaving `build/area01/render/`'s untouched.

The round-2 doc gave 4.3 s / 4.0 s / 13.0 s at load ~150, and the round-2
report gave 10.9 s cold and 6.4 s warm at load ~140. Those runs used up to
8 workers on a loaded machine, and neither figure was taken on the final
file. The table above replaces both.

The fall entry check was itself checked with two scratch mutants of a copy
of em_player_fall.c that move the +7 store after the `request` call in
00164220 and in 00163D50 (`build/area01/render/mut/mutate_fall.py`). The
previous wave's version of this test passes both; this one fails both at
call 0 (actor byte +7 differs at entry).

## 5. Limits and what is left

- **The clip test is unmeasured.** It uses the same DAZ-and-magnitude rule
  as the render context, on both sides, so the test cannot show that the
  hardware agrees. An exponent-255 lane cannot reach it in these routines,
  because the last multiply-add step of the product clamps every operand. The
  "unmeasured" path in the test is a safety net and never triggered.
- **The stack is in main RAM.** The interpreters run every routine and
  callee on a private stack at 0x7F000000, outside both compared regions.
  On the EE, crt0 (`_start`, decomp `src/_start.c`) gives the main thread
  the 1 MiB below 0x01F00000 through SetupThread. The other CreateThread
  users found (func_0010C648's alarm thread, whose stack is the RAM block
  D_00276890, and func_00111740, which takes its stack as an argument) are
  SDK threads. The equivalence arguments below that rest on a callee's
  stack stores assume that the thread running these routines has its
  stack in main RAM, away from the scratchpad and the words the routine
  reads back: `N_070_return_cached` and `N_frame_state_hoist`.
- **Calls are direct.** The captures are end-of-beat images, so each routine
  is called directly on that state at its real call shape. It is not caught
  mid-frame inside its caller.
- **What the entry check covers.**
  - Memory: all of RAM and scratchpad (by pages, section 3), so nothing a
    callee can read is left out. The native worker interpreter's private
    stack holds only the vector argument, which is logged.
  - Registers: the argument registers and stack vectors of each worker's
    signature are compared. Every other register is poisoned on the native
    side, and all cases still pass. That shows no worker's behaviour
    depends on them in these cases; it is not a proof for inputs never run.
  - A store that is overwritten before any load or call cannot be observed
    by any check. The mutation sweep's `gs_2b0_drop_t_store` is such a
    store, and it is equivalent.
- **What "the EE completes" means here.** The model of the EE's access
  rules is alignment plus the TLB map the 12 AREA01 captures hold (section
  3). Cache behaviour and timing are not modelled. The EE, VU and GS
  register pages, 0x1E000000..0x1FFFFFFF and the kernel page pair at
  0xFFFF8000 are mapped on the EE but outside this model, so a case that
  reaches them is refused rather than run; the translations have no view
  for them and fault there. The equivalence arguments that rest on "the
  EE cannot complete" use only a misaligned access and an address no
  valid TLB entry maps. `r4_vif_4fc0_inline_cached_cur` also excludes the
  EE register page by this model limit, as its row says.
- **The alias cases start from constructed states:** cursors placed on
  context words or on D_00275670, context copies, a packet cursor in the
  scratchpad. Each is an execution the EE completes, and each asserts that
  its aliasing happened. They are not states the captures show the game
  reaching.
- **0015B610's callees are scripted.** 00182B30, 00174A50, 00182D70 and the
  five +5 routines are player routines. Their results and effects belong to
  other lanes (00183250 is translated by the AREA01 math lane).
- **Nothing is bound.** Binding the workers to existing port translations
  and wiring the modules into a coordinator is the lead's step. Candidates:

  | worker | existing translation |
  |---|---|
  | 001CB5F0 / 001CB6B0 / 001CB760 / 0021B9A0 | em_packet_chain_original.c |
  | 001CB950 | em_shadow_decal_original.c |
  | 001CFB50 | em_effect_kinds.c |
  | 001CFBE0 | em_head_sprite_original.c |
  | 001F4BF0 | em_status_scene_original.c |
  | 001CAAC0, 001C7900 | em_anim_runtime_rest.c |
  | 001281C0 | em_stream_lanes_original.c |
  | 001C6120 | em_owner_draw_original.c and others |
  | 001D1F80 | em_render_context_live.c |

  No port translation was found for 00121870, 001D2090, 001D4750,
  00122BB8, 001D3990, 00182B30, 00174A50, 00182D70, 00183240, 001833F0,
  00183440 or 001834E0.
- **Unexercised capture shapes.** 00158BD0's node exists only in a01_07,
  which is AREA00 by the end of that beat. 001F4A10 in AREA01 ran from the
  00158D30 node. The unit cases cover the 00158BD0 0x700038B0 word set.

## Known gaps from the close-out spot check (2026-09-26)

The close-out verifier's fresh single-operation mutants that neither run kills. None is a mistranslation (the module passes each killing input); each is a test blind spot, recorded here instead of opening another sweep round.

- C1_f4a10_y_early (em_area01_render_gs.c 001F4A10: the v+4 (y) load is hoisted above the 00122BB8 call). Survives quick and full mode. Killing input: build/area01/closeout_RENDER_1/probe_c1.py rand_y. It is the pinned rand_x case with D_0024295C = v + 4 - 0x58 and the state at v + 4. The oracle completes it on all 7 main beats and the lane passes. The mutant fails the entry check of call 1 (001C7900), where dir.y differs.
- C1_9e60_f_early (em_area01_render_hud.c 001E9E60: the a0+0xB4 (f) load is hoisted above the 9-quadword packet's three word copies). Survives quick and full mode. Killing input: probe_c1.py f_alias. It is the t_alias case with the packet at node + 0xA0, so the +0x14 copy of record +0x4C lands on a0 + 0xB4. The oracle completes it on all 7 main beats and the lane passes. The mutant fails the entry check of call 8 (001CB950): RAM differs at 0x7B21AC.
- C1_tail_vy_early (em_area01_render_vif.c chain_tail: the rec+0x38 (vector y) load is moved before the end tag). Survives quick and full mode. Killing input: probe_c1.py endtag_vy. It is endtag_v shifted by 8: count 3*0x1F8 and the first chunk tag at rec + 8, so the end tag's qwc and id stores land on rec + 0x38. It runs for 001D5170 and 001D4FC0; the oracle completes it on all 7 main beats and the lane passes. The mutant fails the entry check of call 3 (001CAAC0): vector y is 0x4160000E against the original's 0x60600000.
