# The VU1 face morph program (CALL 0x0023C480): CPU translation and proof

Date: 2026-09-24. Lane `vu1-face-morph`. **Nothing here is bound into the
frame or the renderer.** This closes the gap docs/VU1_OBJECT_KERNEL.md
sections 2, 6 and 7 name: Roger's face (and Dennis's in the opening,
handoff and roger-encounter captures) is issued to its own VU1 program, not
to the object kernel. Addresses are boot-ELF addresses; micro addresses are
instruction indices of the uploaded program (micro 0x000 = ELF 0x0023C4B0).
The document holds no original code, data or disassembly.

## 1. Delivered

| File | What |
|---|---|
| `src/game/em_vu1_face_morph.h` | Header-only, pure C translation of the 80-instruction program: `em_vu1_face_morph_mscal` / `_mscnt` (one batch over a 1024-qword VU1 data-memory image, written exactly as the program writes it: the kicked packet, the stale stores and the carried registers included), `em_vu1_face_morph_vertex` (one vertex), `em_vu1_face_morph_position` (the exact morph alone), `em_vu1_face_morph_top` (the double-buffer TOP of block k). It includes `em_vu1_object_kernel.h` for the lane arithmetic (`emvuo_*`, so every translated VU1 program shares one float model), the qword and batch types, and the GS side: the kicked packet has the object kernel's layout, so `em_vu1_object_kernel_decode` and `_triangles` decode it. |
| `tools/test_vu1_face_morph_reference.py` | The reference test (section 5). Default run 3.4 s, `EM_TEST_FULL=1` 11.0 s, `--defects` 87 s (M1, 8 workers, load average about 5 from other lanes). Report: `build/vu1_face_morph_reference/report.json` (counts, addresses, hashes; per face unit in `B_units`). |

## 2. Where it runs

001CAA00 draws an actor's body through 001C7420 (the object kernel) and then
calls 001CB3C0 (the decomp repo's docs/OPENING_ACTORS.md). 001CB3C0 builds
the face unit. The builder column follows 001CB3C0's call order (decomp
`func_001CB3C0.c`, `func_001D3E40.c`); the test checks the uploads, not
the builders:

| Order | Tag | Upload | Builder |
|---|---|---|---|
| 1 | CNT, UNPACK 4 to dmem 0x3F5 | the colour matrix B (1013..1016) | 001C7900 |
| 2 | CNT, UNPACK 8 to dmem 0 | one node: rows 0..3 the head node's position matrix, rows 4..6 its lighting matrix; row 7 is not read by this program | 001C7900 |
| 3 | CNT, UNPACK 2 to dmem 0x3F3 | the morph weights (1011, 1012) | 001CB2C0 |
| 4 | REF 9, REF 1 | a GS packet (DIRECT) and a VIF FLUSH | 001D1F80 |
| 5 | CALL 0x0023C480 | the face packet (below) | 001D3E40 |
| 6 | REF 8 to 0x00816440 + 0x80 * slot | UNPACK 7 to dmem 0x3F9 (1017..1023; the program reads 1020..1023) | 001D3E40 |
| 7 | REF (the face resource + 0x40) | the blocks, each ending in MSCAL 0 (the first) or MSCNT | 001D3E40 |

The test reproduces this recipe from the uploads themselves: every live
input of every captured batch was written by exactly these uploads of its
own unit (report `census.recipes`). The unit is self-contained.

The call that follows a face unit is the next unit's (0x0023C750 in 42
units, 0x00233290 in 6). **A face never gets the clip pass 0x002354A0.**
001D3E40 appends none, so a face vertex outside the guard band is dropped,
not clipped.

**The face packet 0x0023C480** is one DMA CNT whose VIF codes are, in
order, FLUSHA, STCYCL 4,4, STMASK 0, STMOD 0, **BASE 0x20, OFFSET 0x1E5**,
and one MPG of 80 instructions to micro 0. A RET follows. The OFFSET code
resets the double buffer at every CALL, so block k runs at TOP 0x20 (even
k) or 0x205 (odd k). Every captured batch follows this rule.

**The face resources.** Each block is 352 qwords (UNPACK 256 + 96 to TOPS):
32 vertices of 11 qwords.

| Face | Resource | Blocks | Vertices |
|---|---|---|---|
| Roger | 0x018C8740 | 50 | 1,600 |
| Dennis | 0x011749C0 | 55 | 1,760 |

**Where the captures issue it** (22 AREA11 captures, 44 lists). A face CALL
issues the unit; whether anything reaches the screen is the guard band's
call. The counts below are per face unit, from the translation's per-vertex
ADC record (`why[]`, checked against the original program on every vertex,
section 5); the test asserts this grouping unit by unit (report
`B_units`).

Roger's face unit is CALLed in 41 lists (every list except handoff
0x293700). It draws visible vertices (1,048 of its 1,600) only in the
opening, the handoff and route beats 08, 09 and 11..14. In the other 26
units, in 13 captures (playable, both elevator captures, the
startup-reference roger-encounter capture, and route beats 00..07 and 10),
the guard band drops every one of its 1,600 vertices. The startup-reference
`roger-encounter` capture and route beat `14_roger_encounter` are different
captures: Roger's face draws in the beat, not in the startup capture. Every
Dennis unit, live or stale, draws 1,124 of its 1,760 vertices.

| Captures | Roger unit (per list) | Dennis unit (per list) |
|---|---|---|
| `playable_ee.bin`, `elevator/clip47_ee.bin`, `elevator/completed_ee.bin`, route beats 00..07 and 10 | both lists: 0 of 1,600 (every vertex carries the clip ADC; in beats 00 and 10, which were inspected, the face lies left of x/w = 8 px; Roger is off screen in beat 10's screenshot) | none |
| route beats 08, 09, 11, 12, 13, 14 | both lists: 1,048 of 1,600 | none |
| `opening_ee.bin` | both lists: 1,048 of 1,600 | both lists: 1,124 of 1,760; also a stale unit past the write cursor of list 0x293700: 1,124 of 1,760 |
| `handoff_ee.bin` | 0x28F700: 1,048 of 1,600; none in 0x293700 | 0x28F700: 1,124 of 1,760; a stale unit in 0x293700: 1,124 of 1,760 |
| `roger-encounter` (startup-reference) | both lists: **0 of 1,600** (every vertex carries the clip ADC) | both lists: 1,124 of 1,760 |
| `status-hub` | none | none |

That is 48 CALLs: Roger 41 (15 drawing, 26 fully dropped), Dennis 5 live and
2 stale. The count agrees with the object kernel test's census
(VU1_OBJECT_KERNEL.md section 2). Of the 77,920 vertices, 23,588 draw
(Roger 15 x 1,048, Dennis 7 x 1,124). 27,084 carry the data-word ADC (552
per Roger unit, 636 per Dennis unit) and 41,600 the clip ADC (26 x 1,600:
exactly the fully dropped Roger units). A vertex can carry both. In the units
that draw, no vertex carries the clip ADC: only the data word drops their
vertices.

**Census of the uploads** (all 2,435 batches, asserted or reported):
- One template: PRE, PRIM 0x03C, PACKED, NREG 4, REGS TEX0_1 ST RGBAQ XYZF2,
  NLOOP 32, EOP. It is the object kernel's opaque template.
- One fog row (255, 2048, A, B ≠ 0) and one pair of guard rows (the object
  variant of VU1_OBJECT_KERNEL.md section 2). In that pair x = y, scale.w
  is 0 and offset.w is 1.0, so the captured guard w equals c.w; the
  synthetic "guardrow" style is what separates the guard lanes (section 5).
- The ambient row's w lane is always 8388608.0.
- 26 distinct weight rows; 755 batches have all seven weights zero (the base
  face). Lane 1012.w is always 0.
- The data words of every batch address rows 0..6. Some carry bit 14
  (0x4000), which drops out because addresses wrap at 1024 qwords.
- Every drawing TEX0 has TCC 1, TFX 2 and PSM PSMT4: 25 distinct values
  once CLD is masked. Of the 23,588 drawn triangles, 10,560 have TEX0
  values that differ in CLD only; none differ in anything else.

## 3. What the program does

**Data memory read:**

| dmem | Content |
|---|---|
| TOP + 11i + 0 | vertex i's TEX0 qword |
| TOP + 11i + 1 | (s, t, 1, 0) |
| TOP + 11i + 2 | the normal (x, y, z); w unused |
| TOP + 11i + 3 | the base position (x, y, z) and the data word (w) |
| TOP + 11i + 4..10 | the seven position deltas (x, y, z); w unused |
| word + 0..3 / + 4..6 | M, the position matrix, and L, the lighting matrix (word = data word & 0xFFFF, bit 15 = "no kick", addresses modulo 1024) |
| 1011 / 1012 | the weights w0..w3 / w4..w6 (1012.w unused) |
| 1013..1016 | the colour matrix B |
| 1020 | the GIF template |
| 1021 | the fog row (255, 2048, A, B) |
| 1022 / 1023 | the guard scale / offset rows |

**Every batch re-reads 1011..1016 and 1020..1023.** MSCNT resumes at micro
0x04E, which branches back to micro 0x000. So an MSCNT batch is the MSCAL
batch with only the carried registers differing. The object kernel is the
opposite: its MSCNT keeps the MSCAL constants.

**Per vertex** (the header comments name the micro address of each step):
1. **Morph**, lanes x, y, z:
   p = (((((((D0·w0 + D1·w1) + D2·w2) + D3·w3) + D4·w4) + D5·w5) + D6·w6) + base·1).
   Each product is truncated before its add; the base comes last. The
   normal is not morphed.
2. **Position:** c = ((M0·p.x + M1·p.y) + M2·p.z) + M3·1.
3. **Lighting:** l = (L0·n.x + L1·n.y) + L2·n.z, clamped at 0. The program
   also forms lane w, which never reaches an output.
4. **Q** = 1 / c.w.
5. **Guard vector:** g = G0·c + G1·c.w. The CLIP of g.xyz against |g.w|
   goes into a 24-bit history, cleared at the start of every batch.
6. **Fog:** F = max(min(1·A + B·c.w, row.x), 0), and F += row.y when ADC.
7. **ADC:** data-word bit 15, or any CLIP bit of vertices i-2..i (history
   AND 0x03FFFF).
8. **Colour:** ((B0·l.x + B1·l.y) + B2·l.z) + B3·1 in all four lanes,
   capped at 8388863.0.
9. **Outputs**, at TOP + 0x164 + 4i:
   - TEX0: the input qword, every bit;
   - ST: (s·Q, t·Q, 1·Q); w is the carried register's;
   - RGBAQ: the capped colour;
   - XYZF2: ftoi4 of (c.x·Q, c.y·Q, c.z·Q, F).

Steps 3 and 5 to 9 are the object kernel's arithmetic
(VU1_OBJECT_KERNEL.md section 3). Only the morph, the 11-qword vertex, the
lighting's lane w and the schedule are new.

**The packet.** After the loop, TOP + 0x163 receives the template (read
after the last vertex's stores). The program kicks TOP + 0x163 at micro
0x04C: the template and 128 qwords.

**The loop is software-pipelined.** Iteration i
1. reads vertex i's deltas 4..6 and its seven matrix rows;
2. stores vertex i-1's four qwords at TOP + 0x160 + 4i .. 0x163 + 4i, in
   the order RGBAQ, XYZF2, ST, TEX0;
3. reads vertex i+1's base, deltas 0..3, normal and data word, and vertex
   i's ST input and TEX0.

Iteration 0 therefore stores four carried registers (TEX0, ST, RGBAQ,
XYZF2) at TOP + 0x160..0x163. The last of them is then overwritten by the
template. A matrix row that lies in the store area is read as it stood
before the iteration's own stores (synthetic "overlap" cases). The vertex
inputs (TOP..TOP + 351) never overlap the outputs (TOP + 0x160 ..), so the
order of the look-ahead reads against the stores is not observable. The
last iteration's look-ahead reads are dead.

## 4. Arithmetic and timing assumptions

**Float rules.** The float rules are those of the object kernel
(VU1_OBJECT_KERNEL.md section 4). They are the measured VU0 lane rules,
assumed for VU1: DAZ, truncation, FTZ, finite overflow to ±MAX,
multiply-add as a truncated product added to ACC, the (3,3) division, the
raw MAX/MINI order, and the saturating ftoi4. An exponent-255 word that
reaches a live multiply or add faults the batch
(`EM_VU1_FACE_FAULT_OPERAND`, fail-stop). No capture has one. The dead
lanes do not fault: normal w, ST w, delta w, 1012.w and the lighting
matrix's lane w.

**Two latencies decide outputs.** Both are interpreter assumptions. No
save state can confirm them (see below).
- **CLIP flag, 4 cycles.** The CLIP (micro 0x02D) and the flag test (micro
  0x031) are exactly 4 cycles apart on every vertex (89,696 in the full
  run: 77,920 captured and 368 synthetic batches x 32; report
  `latency_windows`). As for the object kernel, this gives the drop window i-2..i.
- **Q, 7 cycles (new for this program).** The division (micro 0x028) and
  the multiply that forms c.xyz·Q (micro 0x02F) are exactly 7 cycles apart
  on every vertex. The instructions in between never stall, and the
  interpreter makes Q visible 7 cycles after the division. With one cycle
  more, every vertex's screen position would use the previous vertex's
  1/w. The program's own schedule supports 7: it puts a no-op at micro
  0x02E, which is exactly what makes the distance 7. The object kernel's
  distance is 8, so it does not depend on this boundary. The defect
  "previous vertex Q" (section 5) shows the test tells the two models apart.

**No PCSX2-executed evidence.** None of the 12 startup-reference save
states, the 16 route states or the 2 truck states holds the face program
in `vu1MicroMem`; their `vu1Memory` holds what later programs uploaded.
States 02 and 03 were checked further, because their buffers looked the
closest to face data. There, dmem 0..7 are not a face's rows, and the data
at TOP 0x205 has the object kernel's 4-qword vertex layout. So no state
lets the header re-run a face batch. The object kernel had state 14; the
face has nothing comparable yet.

## 5. Proof (tools/test_vu1_face_morph_reference.py)

**The oracle** is the object test's `Oracle`: the shadow test's VU1
interpreter with the VU0 lane rules. It executes the ORIGINAL 80
instructions. It adds three records: the CLIP-to-flag-test distance, the
division-to-Q-multiply distance, and each vertex's morphed position as the
next instruction reads it.

**One comparison.** Native and oracle start from the same data memory and
the same carried registers. Every other VF/VI/ACC word is randomised at
each compared batch. At an MSCAL the carried registers are random too.
After the batch they must agree on:
- the XGKICK address and the kicked packet bytes;
- all 16 KiB of data memory;
- the four carried registers;
- all 32 morphed positions;
- the per-vertex record: why[i] != 0 exactly when the program reaches the
  ADC add for vertex i; the CLIP bit of why[i] equals the program's
  clip-flag test over the flag register (i-2..i); clip[i] equals the six
  bits the CLIP produced.

On every replayed batch, compared or not, why[i] holds only the two
`EM_VU1_OBJ_ADC_*` bits, its data bit equals bit 15 of the vertex's data
word, and, when the fog row's x and y are 255 and 2048 (every captured
batch), why[i] != 0 exactly when the kicked XYZF2 of vertex i carries the
ADC bit. So the drawing and ADC counts of section 2 rest on checked data.
The full run checks why[] against the oracle on all 77,920 captured
vertices and 11,776 synthetic ones.

| Section | Full-run result |
|---|---|
| A. Packet | Exactly the code sequence and program in section 2. The packet bytes in RAM equal the ELF in all 22 captures. The builder chain 001CAA00 → 001CB3C0 → 001D3F50 → 001D3E40 and 001D3E40's build of 0x0023C480 are asserted in the ELF. |
| B. Captured lists (22 captures, 44 lists; DMAC + VIF1 replay with CALL/RET, STCYCL, TOPS, MPG, MSCAL/MSCNT) | **All 48 units, 2,435 batches** (48 MSCAL, 2,387 MSCNT) equal: 5.0 MB of packets, 39.9 MB of data memory and 77,920 positions. That includes the 110 batches of the two stale Dennis units. Every batch's inputs come from its own unit's uploads (2,435 of 2,435). The ADC add is reached 54,332 times and skipped 23,588 times. There is no FTOI saturation. The quick run compares blocks 0, 1, 2, 9, 25 and 41 of every unit (288 batches) and replays all 2,435 natively, so the census is always whole. |
| C. Synthetic | 320 cases (368 batches), 20 styles: plain, fog-off, w = 0, negative w, special bit patterns, guard exits, overflow, underflow, wild row addresses, rows over the store slots, exponent-255 words in dead lanes, short NLOOP, MSCNT with new constants, CLIP boundary, random fog-row x and y, random weights, "tail", "guardrow", "signzero" and "zerocolour". The "tail" style puts vertex 31's TEX0 store on dmem 1020, so the template must be read after the last stores. "guardrow" randomises all eight lanes of dmem 1022/1023 (x ≠ y, scale.w ≠ 0, offset.w ≠ 1.0, so the guard w differs from c.w) with a fog row that keeps A + B·w mostly inside 0..255; both CLIP outcomes are asserted (423 of 512 vertices carry the clip ADC in the full run). "signzero" has zero deltas signed so that most products are -0, negative/-0/+0/positive weights and a ±0 base; -0 morphed positions are asserted (387 lanes). "zerocolour" makes the colour and ambient rows signed zeros (per lane all -0, all +0 or mixed); -0 and +0 RGBAQ lanes are asserted (640 and 1,408). TOPs are 0x20, 0x205, 0, 0x20F, random, and 0x21C. FTOI saturates (95 lanes). Both ADC outcomes, the loop exit and the MSCNT resume are reached. Fault cases (15): 12 exponent-255 live lanes fault at vertex 0 (base, delta, both weight rows, normal, ST, M, L, ambient, fog B, guard); MSCNT before any batch, a NULL state, and the position helper's report also fault. |
| D. GS decode | `em_vu1_object_kernel_decode` equals the object test's independent PACKED decode on every compared packet, and `_triangles` equals the strip enumeration. That is 77,920 captured vertices and 23,588 captured triangles. |
| E. TEX0/ST and the port's consumer | Every kicked TEX0 is the input qword. ST x/y equal s·Q and t·Q with the kicked Q, since qword 1 z is 1.0 on every captured vertex. ST w is the carried register. **`em_opening_face_position`** (src/game/em_opening_face.c, the host-float morph used by the current face path) differs from the exact morph on 10,033 of 77,920 vertices: 16,661 lanes (16,653 by 1 ulp, 4 by 2, 4 by 3). It rounds to nearest where the VU truncates. It is not the original morph. |
| Defect injection (`--defects`) | 44 defects in a copy of the header; the quick run catches every one with a failed check (a build error does not count). Morph: delta order, base added first, six deltas, weight 1012 lane, weights 0/1 swapped. Arithmetic: position sum order, lighting clamp, lighting 2 lanes, colour cap, fog clamp order, fog ADC add, fog cap constant, ADC addend constant, fog B lane, guard lane, Q from z, previous-vertex Q, ftoi0. ADC: clip history 2 vertices, history kept across batches, data bit. Schedule: MSCNT keeps MSCAL constants, first stores skipped, rows read after the stores, last vertex not stored, template read before the last stores, XYZF2 not carried, ST w zero, ST from the next vertex, normal from qword 1. Layout: kick offset, store slot, TOP rule. State: MSCNT unchecked. Guard rows (caught only by "guardrow"): guard w lane skipped (g.w = c.w), fog times the guard w, Q = 1/g.w, guard lane y using lane x's scale and offset. Signed zeros: the morph sum started from +0 (caught by "signzero"), the lighting clamped at -0 (caught by "zerocolour"). The per-vertex record (the kicked packet is unchanged): why[] drops the data bit when CLIP is set (caught by the data-word check on captured playable data), why[] zeroed (caught against the program's ADC branch on captured opening data), the why[] clip bit from the current vertex's CLIP only (caught by the ADC-branch check on synthetic data), clip[] zeroed (caught against the CLIP's bits). Equivalent changes are not listed. These are the order of the look-ahead reads against the stores (inputs and outputs never overlap), and ignoring the position helper's fault flag (an exponent-255 morph input always reaches the position multiply as exponent 255, which faults there). |

## 6. What this means for P1

The renderer should draw exactly what this header kicks.
`em_opening_face_position` and the combined body+face asset path
(`em_face_model.c`) are not the original. Their morphed positions differ by
1 to 3 ulp on 13% of the captured vertices, and they do not reproduce the
guard-band drop.

`em_opening_face_position` and `em_vu1_face_morph_position` translate the
same original instructions (micro 0x019..0x020), so the port holds two
translations of one original. Outside this lane: make
`em_opening_face_position` delegate to `em_vu1_face_morph_position` on bit
patterns, or retire it when the face path is bound. The `morph_oracle` in
`tools/test_opening_face_reference.py` calls itself the original VU blend
but rounds to nearest; under the retirement rule for tests that encode
non-original behaviour it should be retired or pointed at this header.

**The recipe for one face unit**, without a VIF interpreter:

1. Build a data-memory image:
   - `dmem[0x3F3..0x3F4]` = 001CB2C0's two weight qwords;
   - `dmem[0x3F5..0x3F8]` = the colour matrix;
   - `dmem[0..7]` = the node rows (tags 1 to 3 of section 2);
   - `dmem[0x3F9..0x3FF]` = the skin record's seven qwords.
2. For block k = 0, 1, …:
   - copy its 352 qwords to `dmem[em_vu1_face_morph_top(k)]`;
   - run `em_vu1_face_morph_mscal` for k = 0 and `_mscnt` after it, with
     one `EmVu1FaceState` per unit;
   - `em_vu1_object_kernel_decode(dmem, batch.kick, …)` gives the GS
     vertices and `em_vu1_object_kernel_triangles` the triangles (class 0,
     the vertex's TEX0).
3. The carried registers never reach a drawn GS field, so a fresh state per
   unit draws the same pixels.
4. No clip pass follows a face (section 2), so nothing else is needed.
   Roger's face is fully drawable once the unit's uploads come from their
   original builders (001C7900, 001CB2C0, 001D3E40 and the face-state
   updater, docs/PLAYER_FACE_HOST.md / OPENING_ACTORS.md).

## 7. Limits

- **VU1 arithmetic and both latencies** (CLIP 4, Q 7) are the interpreter's
  model, not measured (section 4). No save state has the face program.
- **The builders are not executed here.** The test proves the program over
  the captured uploads and that each unit is exactly its own recipe. It
  does not run 001CB3C0 / 001C7900 / 001CB2C0. The owner-draw EE oracle
  stops at an unmodelled MMI instruction (a PEXTLW in 001026D0) on Roger's
  001CAA00, so an executed-unit section like the object test's C needs that
  oracle extended first.
- **No GS pixels.** Texel decoding and rasterization are the renderer's.
- **Beat 15** (the level exit) is out of scope (`in_scope_beat`).
- The two stale Dennis units are compared native against oracle. Their
  inputs are not what the hardware had.

## 8. Makefile hunk (for the chain; the Makefile is not edited here)

```make
.PHONY: test-vu1-face-morph-reference
test-vu1-face-morph-reference:
	python3 tools/test_vu1_face_morph_reference.py

.PHONY: test-vu1-face-morph-defects
test-vu1-face-morph-defects:
	python3 tools/test_vu1_face_morph_reference.py --defects
```

The header needs no build-list change. It includes `game/em_ee_float.h` and
`game/em_vu1_object_kernel.h`. The test also compiles
`src/game/em_opening_face.c` into its private library (section E).
