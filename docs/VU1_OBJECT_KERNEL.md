# The VU1 object kernel (CALL 0x0023C750): CPU translation and proof

Date: 2026-09-24. Lane `vu1-object-kernel`. **Nothing here is bound into the
frame or the renderer.** This is proposal P1's core (docs/OWNER_DRAW.md
section 7): the exact primitives the original draws for every world owner,
the player, Roger and the other actors. Addresses are boot-ELF addresses;
micro addresses are instruction indices of the uploaded program (micro
0x000 = ELF 0x0023C780). The document holds no original code, data or
disassembly.

## 1. Delivered

| File | What |
|---|---|
| `src/game/em_vu1_object_kernel.h` | Header-only, pure C translation of the 62-instruction object kernel: MSCAL and MSCNT batches over a 1024-qword VU1 data-memory image, written exactly as the kernel writes it (the kicked packet, the stale stores and the carried registers included); `em_vu1_object_vertex` for one vertex; `em_vu1_object_kernel_top` (the double-buffer TOP of block k); `em_vu1_object_kernel_decode` (the kicked packet as GS fields) and `em_vu1_object_kernel_triangles` (the strip triangles it draws). Arithmetic from `em_ee_float.h`. |
| `tools/test_vu1_object_kernel_reference.py` | The reference test (section 5). Default run about 8 s, `EM_TEST_FULL=1` about 70 s, `--defects` about 2.5 min (M1, 8 workers). Report: `build/vu1_object_kernel_reference/report.json` (counts, addresses, bands, hashes). |

## 2. Which programs the captured units run

The census below covers both display lists (0x0028F700, 0x00293700) of every
in-scope AREA11 capture: the six startup-reference EE images and route beats
00..14. That is 21 captures and 42 lists.

| CALL target | CALLs | What it is |
|---|---|---|
| **0x0023C750** | **950** | **The object kernel** (this lane). Every unit that 001C7420 and 001CA940/001D38A0 build. |
| 0x002354A0 | 63 | The clip program of a unit whose sphere crosses the view-z plane (001CA7B0 flags & 1, docs/OWNER_DRAW.md). It always follows a 0x0023C750 pass of the same unit. By owner: parachute 20, player 9, husk partner 7, panel/001C4820 6, fan 4, Roger 4 (+1 with 001BB0E0), elevator 4, truck 2, 001C5760 2, crate 2, door 2. **Not translated** (P2). |
| 0x0023C480 | 48 | **The face morph program** (1 node, 11-qword vertices whose position is the base plus seven weighted deltas; weights in dmem 1011/1012). Its CALL is appended by 001D3E40, reached as 001CAA00 → 001CB3C0 → 001D3F50 → 001D3E40 (the decomp repo's `func_001D3E40.c`, NEARMISS, and docs/OPENING_ACTORS.md). Every CALL's model REF is a face resource + 0x40: **Roger's face** (0x018C8740) in 41 CALLs, one in every list except `handoff_ee.bin` 0x00293700; **Dennis's face** (0x011749C0) in 7 CALLs of `opening_ee.bin`, `handoff_ee.bin` and `roger-encounter`, 2 of them in the stale tail past a list's write cursor. So this is a first-level actor draw (Roger's head in every frame). **Not translated here**: P1 cannot draw Roger's face exactly until it is. The test asserts the chain, the address build in 001D3E40 and the attribution (report `census.face_program`). |
| 0x0023C990 | 42 | Inside a RAM-resident sub-list (two per list). Not an object unit. |
| 0x00237180 / 0x00239C90, 0x0023C200 / 0x0023E8A0, 0x00233800, 0x00233290, 0x00231770 | 478 / 436, 166 / 123, 4,536, 252, 143 | Level, receiver and other programs (LEVEL_MATERIALS.md, SHADOW_ORIGINAL.md). |

**Owners of the 950 object-kernel units.** The attribution matches a unit's
model REF to an owner's +0x44 + 0x40. Owners that share one model are named
together.

| Owner | Units | Batches | Owner | Units | Batches |
|---|---|---|---|---|---|
| player (0x8102B0) | 44 | 6,556 | truck | 26 | 1,976 |
| player equipment (0018A6B0) | 288 (+6 shared with 001EA240) | 1,362 (+24) | Roger (008237E0) | 12 (+1 with 001BB0E0) | 1,896 (+158) |
| pickup light (001C5680) | 113 | 113 | elevator | 24 | 864 |
| crate | 82 | 492 | item (00219550) | 58 | 754 |
| panel / 001C4820 | 46 | 598 | parachute | 32 | 288 |
| drum | 32 | 96 | fan | 32 | 64 |
| husk partner | 24 | 96 | husk creature | 19 | 304 |
| door | 21 | 210 | 001C5760 | 24 | 24 |
| pickup | 14 | 14 | 001C5C90 | 8 (+1 with 001BB0E0) | 88 (+11) |
| shadow silhouette, model 0x012D19C0 | 39 | 819 | shadow silhouette, model 0x012DD1C0 | 4 | 52 |

**The same program serves every owner.** The owners differ in three places,
all of them uploaded data: the GIF template at dmem 1020, the fog row at
1021 and the guard rows at 1022/1023. The captures hold exactly three
templates:

| Template (dmem 1020) | Batches | Units |
|---|---|---|
| PRE, PRIM 0x03C (tristrip, Gouraud, textured, fogged, no blending), REGS TEX0_1 ST RGBAQ XYZF2 | 15,851 | every owner, the player, Roger, equipment, items |
| PRE, PRIM 0x07C (the same with ABE 1), same REGS | 137 | pickup light, 001C5760 |
| PRE, PRIM 0x004 (flat tristrip), REGS NOP NOP NOP XYZ2 | 871 | the drop-shadow silhouettes (docs/SHADOW_ORIGINAL.md) |

Every template is PACKED, NREG 4, NLOOP 32, EOP.

The test reproduces the three censuses below from every MSCAL of every list
(report `census`) and asserts them in both modes. Every list is replayed in
full in both modes, so the census is always whole.

**Fog rows.** 813 units (the 770 opaque and the 43 silhouette units) carry
(255, 2048, A, B) with one AREA11 A and B (B ≠ 0). The 137 blended (PRIM
0x07C) units carry (255, 2048, 255, 0), which gives F = 255: no fog. There
are exactly two distinct rows, and every one has x = 255 and y = 2048.

**Guard rows.** The kernel forms g = scale·c + offset·c.w, so lane k passes
CLIP while c.k/c.w lies in [(-1 - offset.k)/scale.k, (1 - offset.k)/scale.k].
There are exactly two variants, and both give g.w = c.w (one has scale.w = 1
and offset.w = 0, the other the reverse):

| Variant | Units | c.x/c.w and c.y/c.w | c.z/c.w |
|---|---|---|---|
| object (PRIM 0x03C and 0x07C) | 907 | [8, 4088] | [-1, 16,777,215], the 24-bit XYZF2 Z range |
| silhouette (PRIM 0x004) | 43 | [8, 4088] | ±8,355,840.5 |

The x and y bounds are window pixels before ftoi4's ×16. An earlier revision
of this section gave the object z bound as 2^23: that is 1/scale.z, not the
band, because the object variant's offset.z (just above -1) shifts it.

**Ambient row.** Every ambient row (dmem 1016) of all 950 units has lane
w = 8388608.0, so A = the light rows' w contributions.

## 3. What the kernel does

The kernel packet is one DMA CNT. Its VIF codes are, in this order, FLUSHA,
STCYCL 4,4, STMASK 0, STMOD 0, **BASE 0x1B0, OFFSET 0x10E**, and one MPG of
62 instructions to micro 0 (the test asserts the exact sequence, that no
code has the interrupt bit and that the tag qword carries no codes). A RET
follows. The OFFSET code resets the double
buffer at every CALL, so block k of a unit runs at TOP = 0x1B0 for even k and
0x2BE for odd k (`em_vu1_object_kernel_top`; every captured batch follows
this rule).

Each model block (0x82 qwords) holds:
- qword 0: STCYCL 4,4 and UNPACK V4-32 128 to TOPS;
- qwords 1..128: the 32 vertices;
- qword 129: MSCAL 0 (block 0) or MSCNT (the other blocks).

**Data memory read:**

| dmem | Content | Read |
|---|---|---|
| TOP + 4i + 0..3 | vertex i: TEX0 qword; (s, t, 1, 0); normal (x, y, z, 0); position (x, y, z, data word) | every batch |
| word + 0..3 | M = node x VP rows (word = data word & 0xFFFF, addresses modulo 1024) | per vertex |
| word + 4..6 | L = C x A rows, the lighting matrix (ACTOR_LIGHTING.md, OWNER_SERVICES.md) | per vertex |
| 1013..1016 | B, the colour matrix: three light colours and the ambient row with the 8388608 bias (001D89D0 via 001C7420) | MSCAL only |
| 1020 | the GIF template | every batch |
| 1021 | the fog row (255, 2048, A, B) | MSCAL only |
| 1022 / 1023 | the guard scale / offset rows | MSCAL only |

**Per vertex** (the header's comments name the micro address of each step).
Every operation is a VU lane operation; section 4 gives the rules.

1. **Position:** c = ((M0·p.x + M1·p.y) + M2·p.z) + M3·1, each product
   truncated and then added. This is the "skinning": one node matrix per
   vertex, selected by the data word.
2. **Lighting:** l = (L0·n.x + L1·n.y) + L2·n.z, then clamped at 0 lane by
   lane. The normal is the authored one, not renormalised.
3. **Colour:** ((B0·l.x + B1·l.y) + B2·l.z) + B3·1 in all four lanes, capped
   at 8388863.0 (2^23 + 255). The low byte of each lane is the GS R, G, B
   and A.
4. **Q** = 1 / c.w (VDIV).
5. **Guard vector:** g = G0·c + G1·c.w. CLIP of g.xyz against |g.w|, shifted
   into a 24-bit history.
6. **Fog:** F = max(min(1·A + B·c.w, row.x = 255), 0).
7. **ADC:** set when the data word has bit 15, or when any CLIP bit of
   vertices i-2, i-1 or i is set (history AND 0x03FFFF). The window rests on
   the CLIP flag latency (section 4). The history is empty
   at the start of every batch. So a triangle with a vertex outside the guard
   band is **never drawn** by this program; it is dropped, not clipped. When
   ADC is set, F += row.y (2048), so bit 15 of the kicked F word is the GS
   ADC bit. **The kernel never culls**: both windings draw.
8. **Output qwords** at TOP + 0x85 + 4i:
   - TEX0: the vertex's qword 0, every bit.
   - ST: (s·Q, t·Q, 1·Q). Lane w is never written by the program: it is
     whatever the register held.
   - RGBAQ: the capped colour.
   - XYZF2 (or XYZ2): ftoi4 of (c.x·Q, c.y·Q, c.z·Q, F).

**The packet.** TOP + 0x84 receives the template. The kernel kicks TOP +
0x84 at micro 0x03A: the template and 128 qwords. The loop is
software-pipelined:
- iteration i stores vertex i-1's four qwords at TOP + 4i + 0x81..0x84;
- it then reads vertex i's ST input;
- it then reads the next vertex's data word, rows, position, normal and
  TEX0.

So iteration 0 writes three carried registers (TEX0, ST, RGBAQ) to TOP +
0x81..0x83, outside the packet. And a node row that lies in the output
region is read by vertex i as it stood after the stores of vertex i-2 (the
look-ahead in iteration i-1 follows that iteration's stores). The
translation reproduces this order (synthetic "overlap" cases). The last
iteration's look-ahead reads are dead.

**MSCNT** resumes at micro 0x03C. That code re-enters the batch setup at
0x00D with the MSCAL registers:
- dmem 1013..1016 and 1021..1023 are **not** re-read, so an upload to them
  between batches does not reach an MSCNT batch (synthetic "mscnt" cases);
- dmem 1020 is re-read every batch.

**The GS side** (`em_vu1_object_kernel_decode` / `_triangles`). Both are
checked against an independent Python decode of the GS PACKED layout on
every compared packet and on 4,000 built tags (section 5, F):
- PRE writes PRIM and so empties the vertex queue. No strip spans two
  packets.
- A vertex i ≥ 2 without ADC draws (i-2, i-1, i), with the TEX0 in force at
  its kick (its own).
- In all 288,387 captured drawn triangles, the three TEX0 values are equal
  or differ only in CLD (bit 61; 151,571 triangles). A triangle's texture is
  therefore the strip's texture.
- Every captured drawing TEX0 has TCC 1 and TFX 2 (HIGHLIGHT), with PSM
  PSMT8 or PSMT4. There are 215 distinct values once CLD is masked. This
  agrees with LEVEL_MATERIALS.md (As = At, since Af = 0).

## 4. Arithmetic

VU1 was never measured (docs/EE_FLOAT_MODEL.md). **The header assumes the
measured VU0 lane rules:**
- DAZ operands;
- exact results truncated toward zero, then FTZ;
- a finite overflow gives ±FLT_MAX;
- a multiply-add is a truncated product added to ACC;
- VDIV form (3,3) for Q;
- MAX/MINI in raw sign-magnitude order, with -0 below +0;
- VFTOI4 saturates by sign.

These are `em_eei_vu_add_raw`, `em_eei_vu_mul_raw`, `em_vu_div_bits`,
`em_vu_max_bits`, `em_vu_min_bits` and `em_vu_ftoi4_bits`. The VU0 operand
clamps depend on the instruction form and were measured for VU0 macro forms
only. So an **exponent-255 word reaching a live multiply or add faults the
batch** (EM_VU1_OBJ_FAULT_OPERAND, fail-stop). No capture has one. The
same word in a dead lane (lighting w, normal w, ST input w) does not fault.

**What the captures say about the assumption.** The shadow test's VU1
interpreter uses host double arithmetic and wraps FTOI. It was run unchanged
on 16,713 captured batches. It disagrees with the VU0-rule oracle in two
places only:

- **50 XYZF2 words.** These are FTOI results outside int32 (saturated here,
  wrapped there). Every one lies on an ADC vertex.
- **6 ST words**, on two vertices of the player equipment in beat 10. There
  c.w has an addend of 1.1e-16 against about 4.3. The double sum rounds that
  addend away; the exact sum truncated moves c.w by one ulp, and Q by 2 ulps.
  The XYZF2 words are equal.

The translation follows the VU0 rules there. That is the PCSX2 behaviour
measured on VU0, and the INI gives VU1 the same settings. It remains an
assumption until VU1 is measured.

**Partial VU1-executed evidence: PCSX2 save state 14.** Of all the
startup-reference and route save states, only
`SCUS-97112 (0AE679AF).14.p2s` has the object kernel in `vu1MicroMem`. Its
`vu1Memory` holds both double-buffer blocks after PCSX2's VU1 ran them
(PRIM 0x03C). Running `em_vu1_object_kernel_mscal` over that image at TOP
0x1B0 and at TOP 0x2BE (the MSCAL constants are still in dmem 1013..1023)
reproduces PCSX2's template qword and **all 480 of 480 output lanes** of
each block. The ST w lanes are excluded because they hold the carried
register. The blocks have 16 and 4 drawing vertices. This covers the
arithmetic on ordinary values only. It does not reach the 2-ulp ST case,
saturated FTOI, or a clip ADC (all ADC vertices in the image are data-bit
ADC). It is also PCSX2's VU1, not hardware.

**The clip-flag latency is an interpreter assumption too.** The drop window
i-2..i depends on the oracle's model that a CLIP judgement becomes visible
to the flag test 4 cycles after the CLIP issues. The test measures the
distance on every compared vertex: CLIP (micro 0x027) and the flag test
(micro 0x02B) are exactly 4 cycles apart on all 602,816 vertices. That is
the boundary itself, so if the true latency were 5 cycles, the window would
become i-3..i-1 and the oracle and an equally wrong translation would still
agree. What supports i-2..i:
- the mask 0x03FFFF keeps exactly three 6-bit judgements;
- i-2..i is the window whose drop matches the triangle (i-2, i-1, i) that
  vertex i's kick draws, so a vertex outside the guard band removes every
  triangle that uses it;
- 4 cycles is the same pipeline latency the interpreter gives every VF
  result, and the program's own spacing (the flag test sits exactly that
  far after CLIP on every vertex) suggests it was scheduled for it.

No original capture confirms it yet. The save-state image has no clip-ADC
vertex. Confirming from a route beat's `gs.bin` needs a rasterized
comparison of a triangle the two windows decide differently, which is P1's
renderer work.

Every saturated FTOI lane in the captures lies on an ADC vertex: 4,002 lanes
on 2,217 vertices, **none on a drawing vertex**. The test asserts this.

## 5. Proof (tools/test_vu1_object_kernel_reference.py)

**The oracle.** The shadow test's VU1 interpreter (`sh.VU1`) models:
- in-order issue and VF operand stalls;
- CLIP flags visible 4 cycles after their producer, and Q 7 cycles after
  DIV;
- micro memory filled by the VIF's MPG;
- XGKICK snapshots.

Its arithmetic is replaced by the VU0 lane rules of
tools/ee_float_model.py. It executes the ORIGINAL 62 instructions.

**What one comparison checks.** The native header and the oracle start
from:
- the same data memory;
- the same carried registers: every VF/VI/ACC word is randomised at each
  MSCAL, so a dependence on an unwritten register would show.

After the batch they must agree on:
- the XGKICK address;
- the kicked GIF packet bytes up to EOP;
- all 16 KiB of data memory.

The quick run keeps every case class and a sample of the units. In the full
run the counts are:

| Section | Full-run result |
|---|---|
| A. Kernel packet | The exact code sequence and program as above. The packet bytes are identical to the ELF in all 21 captures. |
| B. Captured lists (21 captures, 42 lists, DMAC + VIF1 replay with CALL/RET, STCYCL, TOPS, MPG, MSCAL/MSCNT) | **All 950 units, 16,859 batches** (950 MSCAL, 15,909 MSCNT) equal: 34.8 MB of packets and 276 MB of data memory. 539,488 vertices: 304,929 drawing, 212,026 with the data ADC, 35,577 with the clip ADC (a vertex can have both). Every value the kernel reads comes from an upload the replay saw, except 149 batches of one unit past the DMA write cursor of `opening_ee.bin` list 0x00293700. That unit is a stale, half-overwritten tail of the list being built; its batches are still compared, and the test asserts that no other unit has an unknown input. |
| C. Executed owner units (docs/OWNER_DRAW.md lane D) | The ORIGINAL 001CAA00 over all 255 owner-frames of beats 00..14. The **119 drawn units** (1,691 batches, 14 of them clip units) are replayed on a fresh VU1 and all equal. Each unit is self-contained: the kernel reads nothing it did not upload. This includes the three pickup units missing from the captured lists. By owner: crate 26, fan 12, parachute 11, drum 9, truck 9, door 8, elevator 8, husk partner 8, husk creature 7, panel 7, 001C4820 7, pickup 7. |
| D. Synthetic | 240 cases (288 batches), 15 styles: plain, fog-off, w = 0, negative w, special bit patterns, guard-band exits, overflow, underflow, wild row addresses (mod 1024), rows over the stale-store and output slots, exponent-255 in dead lanes, short NLOOP, MSCNT after new constants, CLIP-boundary coordinates (±1 exactly against w = 1), and random fog-row x and y lanes (every capture has 255 and 2048, so only this style shows that the cap and the ADC addend are read from the row). Both outcomes of the ADC branch, loop exit and MSCNT resume are reached, and FTOI saturates. 8 exponent-255 live-lane cases fault as required, as do MSCNT without MSCAL and a NULL state. |
| E. The port's CPU consumers on every captured batch | `em_lighting_vertex` (em_lighting.c) equals the kicked RGBAQ on **all 539,488 vertices**. `em_shadow_gs_object_batch` (em_shadow_gs.h) equals every XYZF2 lane of every drawing vertex and every ADC decision, and differs in 197 lanes of ADC (never drawn) vertices. Both use host double arithmetic, and on synthetic inputs `em_shadow_gs_object_batch` also differs on drawing vertices (5 lanes, 4 ADC decisions). They are exact on the captured data but are not the kernel. |
| F. TEX0/ST path (OWNER_DRAW.md section 6 listed it as unverified) | For every compared vertex: the kicked TEX0 qword equals the vertex's qword 0 (128 bits); ST x/y equal s·Q and t·Q, with the Q the kernel wrote in ST z (qword-1 z is 1.0 on all 511,616 captured vertices); ST w is the carried register. Recovering s as S/Q in host arithmetic lands within 2 ulps (0: 154,876, 1: 119,442, 2: 3,049). No test here decodes texels. |
| F. GS decode and triangles (the renderer's entry points) | On every compared packet of B, C and D, all three templates, `em_vu1_object_kernel_decode` equals an independent Python decode of the PACKED layout in every field of every vertex: TEX0 (64 bits); S, T, Q; the low bytes of R, G, B, A; X, Y; Z (24 bits for XYZF2, 32 for XYZ2); F; ADC. It also equals it in the returned count, PRIM and REGS, and it writes nothing past NLOOP. That is 511,616 XYZF2 vertices and 27,872 XYZ2 vertices in B. `em_vu1_object_kernel_triangles` equals the enumeration "every i ≥ 2 whose ADC is 0": 288,387 textured and 16,542 silhouette triangles in B, 27,883 in C. It is also checked on 4,000 built tags. Half are accepted, covering all 16 slot combinations, PRE on and off, any PRIM, NLOOP 0..32, random reserved bits and kicks that wrap dmem. The other half each break one rule (NLOOP > 32, FLG ≠ 0, NREG ≠ 4, a register outside its slot's choices, EOP clear) and must return -1 with nothing written. Triangles return ~0 for any PRIM that is not a strip. |
| S. PCSX2 save state 14 (section 4) | Both double-buffer blocks of the image: the template and 480 of 480 output lanes per block equal PCSX2's VU1 output. Extracted once with the decomp venv's `extract_zstd_entry` into `build/vu1_object_kernel_reference/states/`; skipped with a reason if the state or the venv is missing. |
| Clip-flag window | CLIP and the flag test are exactly 4 cycles apart on all 602,816 compared vertices (B, C and D). The test fails on any other distance (section 4). |
| Defect injection (`--defects`) | 46 defects in a copy of the header. The quick run catches all of them with a failed check; a build error does not count as caught. Kernel (22): sum order, lighting clamp, colour cap, fog clamp order, fog ADC add, clip-history width, data bit, Q lane, ST w, MSCNT reloading constants, skipped first stores, ST read before the stores, look-ahead before the stores, ftoi0, kick offset, CLIP ≥, guard lane, lighting lane count, history kept across batches, fog w lane, the fog cap hard-coded to 255, the ADC addend hard-coded to 2048. Decode and triangles (24): F shifted by 5, R/B swapped, G from R, A from B, Z masked to 16 bits, Q from lane w, S from T, XYZ2 Z shifted, X from the Y word, Y from the X word, ADC from bit 14, TEX0 low word only, NLOOP > 32 accepted, EOP / FLG / NREG ignored, slot 0 or 3 unchecked, PRE ignored, XYZ2 taken as XYZF2, ST slot always set, triangles from i = 3, triangles ignoring ADC, triangles for any PRIM. (The stricter criterion showed that the earlier revision's "ST w zero" defect had only broken the build; it now compiles and is caught by the packet comparison.) Changing the order of the four final stores is equivalent (distinct addresses), so it is not listed. |

## 6. What this means for P1 (the chain's decision)

The renderer should draw exactly what this header kicks. It should not
recompute positions and colours with `em_shadow_gs_*` and
`em_lighting_vertex`: those are exact on the captured drawing vertices but
are not translations.

The recipe for one unit, without a VIF interpreter:

1. Build a data-memory image:
   - `dmem[0x3F5..0x3F8]` = tag 1's payload;
   - `dmem[0 .. 8n-1]` = tag 2's payload;
   - `dmem[0x3F9..0x3FF]` = the skin record's 7 qwords;
   - `dmem[0x3FD]` = the REF 2 row when 001D37D0 emitted it.
2. For model block k (k = 0, 1, …):
   - copy its qwords 1..128 to `dmem[em_vu1_object_kernel_top(k)]`;
   - run `em_vu1_object_kernel_mscal` for k = 0 and `_mscnt` after it,
     with one `EmVu1ObjState` per unit;
   - `em_vu1_object_kernel_decode(dmem, batch.kick, …)` gives the GS
     vertices;
   - `em_vu1_object_kernel_triangles` gives the triangles to rasterize, in
     class 0 (or ABE for PRIM 0x07C), with the vertex's TEX0.
3. The carried registers never reach a drawn GS field, so a fresh state per
   unit draws the same pixels. Only the undrawn ST w lane and the TOP +
   0x81..0x83 slots depend on them.
4. When 001CA7B0 flags & 1, the unit also runs 0x002354A0 (P2, not
   translated). Until then the unit cannot be drawn exactly and must stay
   unwired (fail-stop).
5. Roger's face (and Dennis's in the opening, handoff and roger-encounter
   captures) is a separate CALL of 0x0023C480 from 001CB3C0 (section 2).
   This header does not draw it. Until that program is translated, the face
   must stay unwired.

Section 5 C shows this recipe's inputs are exactly the unit's own uploads.
The recipe itself is the header's MSCAL/MSCNT API, checked in B and C
through the real VIF replay.

## 7. Limits

- **VU1 arithmetic** is the VU0 rule set, assumed (section 4). The only
  VU1-executed check is PCSX2's output in save state 14 (480 of 480 lanes
  per block, ordinary values only). Exponent-255 operands on live lanes
  fault rather than guess.
- **The clip-flag latency** (4 cycles, so the drop window is i-2..i) is the
  interpreter's model and is not confirmed by an original capture
  (section 4).
- **Not translated:** the clip program 0x002354A0 (P2) and the face morph
  program 0x0023C480 (Roger's face, a first-level draw).
- **No GS pixels.** This lane proves the kicked packets. Texel decoding
  (TEX0 → texture) and rasterization are the renderer's (LEVEL_MATERIALS.md
  covers the class-0 state).
- **Beat 15** (the level exit, area 1) is out of scope (`in_scope_beat`).
- The 149 stale-unit batches of `opening_ee.bin` are compared oracle against
  native. Their inputs are not what the hardware had.

## 8. Makefile hunk (for the chain; the Makefile is not edited here)

```make
.PHONY: test-vu1-object-kernel-reference
test-vu1-object-kernel-reference:
	python3 tools/test_vu1_object_kernel_reference.py

.PHONY: test-vu1-object-kernel-defects
test-vu1-object-kernel-defects:
	python3 tools/test_vu1_object_kernel_reference.py --defects
```

The header needs no build-list change. It includes only `game/em_ee_float.h`.
