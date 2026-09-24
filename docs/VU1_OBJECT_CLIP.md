# Object-kernel clip pass (VU1 program of 0x002354A0)

Date: 2026-09-24. Lane `vu1-object-clip`. **Nothing here is bound into the
frame.** This is proposal P2 of docs/OWNER_DRAW.md section 7: the guard-band
clip pass an owner unit runs when 001CA7B0's flags have bit 0 set. The
translation is header-only and verified kick for kick against the original
microcode. Drawing its triangles is the binding chain's job (P1,
`em_gfx_object_unit`).

This document holds addresses, field names, packet codes and counts only. It
holds no original code, data or disassembly.

## 1. Delivered

| File | What |
|---|---|
| `src/game/em_vu1_object_clip.h` | Header-only translation of the program: `em_vu1_object_clip_run` (one batch; every XGKICK snapshotted), `em_vu1_object_clip_image` / `em_vu1_object_clip_batch` (the data memory from a unit's own pieces), `em_vu1_object_clip_triangles` (the kicks decoded into GS triangles). It includes `em_vu1_shadow_clip.h` for the VU arithmetic helpers, so every translated VU1 program shares one float model. |
| `tools/test_vu1_object_clip_reference.py` | The original-instruction oracle (section 5). Default run 7 to 9 s, `EM_TEST_FULL=1` 30 to 50 s, depending on load (M1). |

## 2. Where it runs

001CA940 with `flags & 1` runs 001D3C30 and then 001D3BA0. 001D3BA0 emits the
plain object pass (skin record 0, the CALL to 0x0023C750, the model REF). Then
001D3AD0 appends the clip pass (OWNER_DRAW.md section 3):

- REF 8 qwords to skin record 1 (0x00816540 + 0x80 * slot), uploaded to dmem
  1017..1023;
- REF 1 qword to the arena (a VIF FLUSH);
- CALL 0x002354A0;
- the fog-off REF 2 when context +0x0C bit 0 is clear (in no AREA11 capture);
- the model REF again.

**The kernel packet 0x002354A0** (checked by the test, section P):

- a CNT of 459 qwords;
- STCYCL 4,4, BASE 0x1B0 and OFFSET 0x84;
- four MPG blocks at micro 0x000, 0x100, 0x200 and 0x300: 910 instructions.

So the batches alternate between TOP 0x1B0 and 0x234. The object kernel packet
0x0023C750 has the same BASE and OFFSET 0x10E. The first block's MSCAL 0 starts
the program. Each MSCNT resumes at micro 0x036, which branches to 0x000, so
every batch runs the same code from the start.

## 3. What the program does

Micro addresses are instruction indices of the uploaded program. "dmem N" is a
VU1 data-memory qword.

**Inputs** (all uploaded by the unit):

| dmem | Content |
|---|---|
| 0 .. 8n-1 | per node: rows 0..3 the position matrix (node x VP), rows 4..6 the lighting matrix (C x A) |
| 1013..1016 | the colour matrix B |
| 1017 | triangle-list GIF tag (skin record 1): PRE, PRIM 0x03B, NREG 9, REGS ST RGBAQ XYZF2 three times |
| 1018 / 1019 | a GIF tag with NLOOP 1, EOP, NREG 1, REGS TEX0_1, and its data qword |
| 1021 | fog row (255, 2048, A, B) |
| 1022 / 1023 | guard rows |
| TOP..TOP+127 | 32 vertices of 4 qwords: TEX0 value, (s, t, 1, 0), normal, (x, y, z, data word) |

**Outputs.** The program writes these dmem areas:

- 994: the saved loop registers;
- 1019;
- the working area: the packet tag at 696 and 3-qword vertex slots (ST, RGBAQ,
  XYZ) from 697.

**Steps:**

1. **Loop** (0x000..0x036), over the 32 vertices:
   - c = p x M. M is the four qwords at the data word's address: its low
     16 bits, wrapping at 1024 qwords, so bits 10..14 drop out.
   - g = dmem 1022 * c + dmem 1023 * c.w.
   - The CLIP of g.xyz against |g.w| goes into the 24-bit history.

   Vertex i enters the clipping code (0x04B) when all three hold:
   - its data word has bit 15 clear;
   - the history of vertices i-2..i is non-zero;
   - the three are not all outside one guard plane (six flag tests).

   There is no i >= 2 test and no back-face test (unlike the shadow clip
   kernels 00239C90 / 0023E8A0).
2. **Entry.** The program saves the loop registers and sets dmem 1019 to
   vertex i's qword 0. It kicks dmem 1018, which gives TEX0_1 = vertex i's
   TEX0. Vertices i-2, i-1 and i each go through the vertex routine (0x1E6),
   each with its own node:
   - XYZ slot = p x M (clip space);
   - ST slot = qword 1;
   - RGBAQ slot = min(max(n x L, 0) x B, 8388863) per lane (the x B product
     with w = 1: B's row 3 is added once).
3. **Plane w = 0.1** in clip space (0x077..0x0AF, cases 0x220 and 0x25B):
   - all three behind: the entry kicks nothing more;
   - two behind: both move onto the plane along their edges to the vertex in
     front;
   - one behind: the triangle splits in two.

   Each new point is v + (u - v) * (0.1 - v.w) / (u.w - v.w), applied to all
   three slots.
4. **Projection** (0x0B0): Q = 1 / w. XYZ.xyz and ST.xyz are multiplied by Q.
   The XYZ slot keeps its clip w for the fog.
5. **Screen planes** x = 4088, x = 4, y = 4088 and y = 4 (0x0BE..0x1AD; cases
   0x2A1 / 0x2D9 for x, 0x319 / 0x351 for y, 0x382 for three out). Each plane
   covers the triangles present when it starts:
   - one vertex out splits the triangle, and the new one is appended;
   - two out move both vertices;
   - three out collapse the triangle onto (2048, 2048, 0, 0), a zero-area
     triangle.

   There is no triangle cap. The w plane leaves at most 2 triangles and each
   plane at most doubles them, so at most 32 triangles fill slots 697..984.
   The shadow kernels abort above 9.
6. **Output** (0x1AF..0x1E4), per slot:
   - z = min(z, 8388607);
   - F = max(min(A + B * w, 255), 0);
   - XYZ = the four lanes to fixed point with 4 fraction bits, which is what
     PACKED XYZF2 reads: X, Y, Z << 4, F << 4, with ADC always clear;
   - RGBAQ = the four lanes to integers (the GS takes the low bytes).

   dmem 696 = dmem 1017 with NLOOP n | EOP. The program kicks 696, restores
   the loop registers and continues the loop. After the loop it kicks
   dmem 1018 once more, with whatever dmem 1019 holds. That can be the last
   entry's TEX0 from an earlier batch of the unit.

**What one batch sends to the GS:**

- per entry, a TEX0_1 write (vertex i's qword 0);
- unless all three vertices were behind, one packet of n triangles with PRIM
  0x03B: triangle list, Gouraud, textured, fogged, no blending. Each vertex
  carries ST (Q = 1/w in the ST z lane), RGBAQ and XYZF2;
- a final TEX0_1 write.

Each clipped triangle is textured with its vertex i's TEX0. That is the TEX0
in force when the object kernel draws the same triangle at vertex i.

**Relation to the object pass** (checked on every captured batch, section 5):

- The object kernel 0023C750 sets ADC on vertex i exactly when the data word
  has bit 15 set, or the guard-band clip history of vertices i-2..i is
  non-zero.
- The clip pass enters exactly the vertices whose ADC comes from the guard
  band alone, less the triangles lying wholly outside one guard plane.

So together the two passes draw every strip triangle (a vertex i with bit 15
clear) at most once. The exceptions, which neither pass draws with any area:

- triangles lying wholly outside one guard plane (rejected in the loop);
- entries whose three vertices are all behind w = 0.1 (the abort at 0x1DE:
  the entry kicks only its TEX0);
- triangles, or parts of them, that a screen plane finds wholly outside and
  collapses onto (2048, 2048, 0, 0): they are kicked, but with zero area.

## 4. Native API

```c
EmVu1Qword dmem[1024];
em_vu1_object_clip_image(dmem, nodes /* tag 2 payload, 32 words per node */, n,
                         color /* tag 1 payload, 16 words */,
                         record /* skin record 1, 28 words; dmem 1021
                                   replaced when the REF 2 is emitted */);
for (k = 0; k < blocks; ++k) {
    int32_t top = em_vu1_object_clip_batch(dmem, k, model + 0x40 + 0x820 * k);
    EmVu1ObjectClipResult r = { .qw = storage, .capacity = EM_VU1_OBJECT_CLIP_MAX_QWORDS };
    em_vu1_object_clip_run(dmem, (uint32_t)top, &r);            /* keeps dmem */
    n = em_vu1_object_clip_triangles(&r, tris, EM_VU1_OBJECT_CLIP_MAX_TRIANGLES * 32);
}
```

Every function returns -1 and fails stop on bad input:

- `em_vu1_object_clip_batch` refuses a block whose VIF codes are not STCYCL +
  UNPACK 128 + MSCAL/MSCNT;
- `em_vu1_object_clip_run` faults on an FTOI outside int32 (the hardware
  result is not established) and on a packet without EOP;
- `em_vu1_object_clip_triangles` refuses anything the program does not kick:
  a non-PACKED tag, another register, ADC set, a PRIM that is not a triangle
  list, or a missing TEX0.

`EmVu1ObjectClipTriangle` carries:

- TEX0;
- PRIM;
- the entry vertex;
- per vertex: S, T, Q (floats), R, G, B, A (bytes), X, Y (GS 12.4), Z (24
  bits) and F.

A result's qword storage is 9,314 qwords at most (149 KB). The caller
supplies it.

## 5. Verification

`python3 tools/test_vu1_object_clip_reference.py`. The oracle is the shadow
lane's VU1 interpreter (docs/SHADOW_ORIGINAL.md F). It runs the ORIGINAL
program loaded from the kernel packet's MPG blocks. Its model: in-order
issue, VF stalls, flags 4 cycles after the op, Q through WAITQ, truncated
binary32, XGKICK snapshots.

**A. The 14 captured clip units** of OWNER_DRAW.md section 5, found again
through the owner-draw oracle:

- the ORIGINAL 001CA7B0 classifies 255 owner-frames;
- the ORIGINAL 001CAA00 builds each clip unit;
- each unit is located exactly once in the captured list.

The test asserts that the 14 (beat, behaviour) pairs are the documented ones.
It then replays each captured unit through DMAC/VIF1: CNT payloads from the
list, REF targets from RAM, the CALLed kernel packets from the ELF. The
object kernel runs, then the clip program. On each of the 243 clip batches:

- **Kick for kick.** The translation over the interpreter's dmem makes the
  same kicks: address, order and every packet byte (1,045 kicks, 128,112
  bytes). It reaches the same entries and leaves all 1,024 dmem qwords
  equal. Each kick's `vertex` is checked too: the entry's vertex i for the
  1018 and 696 kicks, 32 for the kick after the loop.
- **Native image.** `em_vu1_object_clip_image` and `em_vu1_object_clip_batch`
  build the image from the unit's colour CNT, node CNT, skin record 1 and
  model blocks. Run in order on one image, it kicks the same. So the program
  reads nothing the unit does not upload, and the object pass changes
  nothing it reads. Built again over a buffer of random bytes, the image is
  identical: every qword the pieces do not fill is zeroed.
- **Random registers.** The interpreter runs again on that image with every
  VF/VI register, ACC, Q, I and the clip flags random, and kicks the same. No
  state carries in from an earlier program.
- **Decode.** `em_vu1_object_clip_triangles` equals an independent Python
  decode of the interpreter's packets (702 triangles). Every PRIM is 0x03B.
- **Relation to the object pass.** The relation in section 3 holds on every
  vertex of every batch. The batches have 5,996 object-kernel ADC vertices;
  2,163 vertices are rejected because the whole triangle lies outside one
  guard plane.

| Unit (beat, behaviour) | Batches | Entries = triangle packets | Triangles |
|---|---|---|---|
| 00 parachute 00823E80 | 9 | 27 | 49 |
| 01 parachute | 9 | 61 | 106 |
| 02 parachute | 9 | 0 | 0 |
| 02 panel 00159210 | 13 | 6 | 9 |
| 02 elevator 00827B10 | 36 | 0 | 0 |
| 03 parachute | 9 | 27 | 49 |
| 04 parachute | 9 | 30 | 58 |
| 04 truck 00823FF0 | 76 | 145 | 219 |
| 05 elevator | 36 | 50 | 116 |
| 05 001C4820 | 13 | 0 | 0 |
| 08 crate 001551B0 | 6 | 25 | 36 |
| 09 husk partner 00827490 | 4 | 6 | 9 |
| 09 door 001BC350 | 10 | 24 | 51 |
| 11 husk partner | 4 | 0 | 0 |

The captured batches reach every clipping case:

| Case | Hits |
|---|---|
| w two behind | 45 |
| w one behind | 52 |
| x two out | 216 |
| x one out | 182 |
| y two out | 101 |
| y one out | 67 |
| collapsed | 28 |

No captured entry has all three vertices behind w = 0.1, no captured FTOI
leaves int32, and no captured output vertex has A + B w above 255 (the
upper fog clamp is inactive in every capture; section C's fogmax style
covers it).

**B. Every other intact clip unit in the captured memory.** A unit is intact
when its CALL 0x2354A0 tag walks back to the colour CNT. The test scans both
display lists of the 15 route beats and of the startup-reference captures,
and deduplicates units by content. That gives 50 units:

- 1-node units with models 0x01350680, 0x01369A00, 0x01392F80, 0x01397A80,
  0x013AA000, 0x013AA900, 0x013B4D00, 0x01784C80 and 0x01792080 (the last
  two in 15_level_exit);
- 2-node units with models 0x0135B300 and 0x013B5E00;
- two 21-node units, with models at 0x00D1C200 (149 blocks) and 0x01877780
  (158 blocks). The clip pass enters nothing in either.

The full run checks all 50 units: 3,193 batches, 4,495 kicks, 651 packets
and 1,113 triangles. They pass the same checks as A, including the native
image and random registers on every batch. The quick run takes one unit per
node count.

**C. Synthetic sweep.** The batches are built on the 09 husk partner's image
(2 nodes). Each vertex is solved back through its node's position matrix
from a GS-space target (X, Y, w). Every batch starts with random registers.
The styles:

- near (w around 0.1);
- wide;
- huge;
- mix;
- behind;
- far (A + B w below 0, the max(F, 0) clamp);
- flags (random data-word bits 10..15, including entries at i = 0 and 1,
  which read the qwords below TOP);
- slots (both nodes);
- fog-off (row (255, 2048, 255, 0));
- ftoi (the z column times -1e7, or B times -1e12);
- fogmax (fog A = 400, B = -10, so A + B w is above 255 for w below about
  14.5: the min(F, 255) clamp). The test reads the pre-clamp value at micro
  0x1BE, where the program takes the minimum with the fog row's x lane, and
  requires it above 255 on some output vertices (6,230 in the full run, 716
  in the quick run);
- boundary. Node 0's position matrix is exact, so c = (p.x, p.y, 1000, p.z)
  and w = p.z. Every three consecutive vertices hold one boundary vertex,
  one inside the screen and one far outside x = 4088 (so the entry is
  taken). A boundary vertex sits exactly on w = 0.1 (the program's binary32
  0.1) or, at w = 1 (Q = 1 exactly), exactly on x = 4088, x = 4, y = 4088
  or y = 4. There the difference the program tests is +0, which counts as
  in front / inside. The test also runs the interpreter with every boundary
  vertex moved one ulp to the outside and requires different kicks, so the
  tie-break decides the output (48 of 48 batches).

The full run has 576 batches: 26,376 kicks, 12,887 packets and 40,793
triangles, with up to 20 triangles per entry. 24 FTOI-overflow batches fault
the translation at exactly the kick where the interpreter wraps; the kicks
before it are equal. The quick run has 72 batches. Every micro address is
traced, and both outcomes of all 50 conditional branches are reached, in
quick and full mode.

**Defect injection** (2026-09-24). 34 mutations of the header were each run
with `EM_VU1_OBJECT_CLIP_SRC`. The quick run catches 32. The mutations cover:

- the lighting clamp, the colour clamp lanes, a lighting row lane, the ST
  slot, the x B operand;
- the w limit, the one-behind slots and colour, the one-behind count;
- the projection w lane, the ST lane;
- the 4088 limit, the plane order, the plane iterating over appended
  triangles, the collapse point;
- the z clamp, max(F, 0), the fog A/B lanes, the FTOI scale, NLOOP;
- the TEX0 store/kick order, the flag mask, the history mask, the plane
  rejects;
- the vi12 reload, the guard lane, the previous-vertex order, the final kick;
- the decoder's Q, the image's TOP and colour rows.

The two survivors are equivalent:

- Swapping the two behind vertices in a two-behind case moves each one
  independently along its edge to the same front vertex.
- The vi10 restore at 0x1DE is dead: the loop reloads vi10 before it uses
  it.

A review found five further mutations that the first sweep missed, each
surviving both runs: the upper fog clamp taken from the fog row's y lane
(2048) instead of x (255), a zero difference counted as behind in the
w-plane test, a zero difference counted as outside in the screen-plane
test, the final kick's `vertex` set to 31, and the image without its
clearing. The fix round added the fogmax and boundary styles and the
kick-vertex and dirty-image checks, then ran 10 mutations in quick mode, all
caught:

| Mutation | Caught by |
|---|---|
| upper fog clamp from the 2048 lane | fogmax kicks |
| upper fog clamp removed | fogmax kicks |
| w plane: +0 counts as behind | boundary kicks |
| screen planes: +0 counts as outside (all four) | boundary kicks |
| the same, x = 4088 / y = 4088 planes only | boundary kicks |
| the same, x = 4 / y = 4 planes only | boundary kicks |
| final kick `vertex` 31 | kick vertex (captured units) |
| 696 kicks tagged with the next vertex | kick vertex (captured units) |
| image without clearing | dirty image |
| image clearing only dmem 0..999 | dirty image |

The first sweep's 34 mutation trees were removed after that round, so they
were not re-run against the new test. The test only gained checks, so every
mutation it caught before is still caught.

**UBSan.** The shim is built with `-fsanitize=undefined` in recoverable
mode, logging to `build/vu1_object_clip_reference/ubsan.*`. A report fails
the run, and an injected signed overflow does fail it.

## 6. Limits and open items

- **The float model is the interpreter's.** VU1 rounding was not measured.
  docs/EE_FLOAT_MODEL.md measured VU0, and its VU1 row says the INI settings
  are the same. The translation equals the interpreter bit for bit: chop,
  denormals flushed, ±MAX clamps, DIV by zero ±MAX. No captured batch holds
  an Inf, NaN or denormal operand, so the operand-mapping rules are the
  shadow header's assumption (as there).
- **FTOI outside int32** is not established. The translation faults there. No
  captured batch reaches it.
- **Kicks are snapshots** taken at the XGKICK, as in the interpreter. On
  hardware, PATH1 reads VU memory while the program keeps running. The
  snapshot model holds when no kicked qword is rewritten before its
  transfer ends. Traced over the synthetic sweep, the kicks are at micro
  0x058 (dmem 1018), 0x1D9 (dmem 696) and 0x032 (dmem 1018 after the loop).
  dmem 1019 is written only at 0x050. The 696 packet (tag and slots) is
  written only inside an entry, after that entry's XGKICK at 0x058.
  - The 696 packet is safe, because an XGKICK waits for the previous
    transfer to end. The same holds for dmem 1019 after an entry that
    kicked its triangles: its kick at 0x1D9 comes before the next write at
    0x050.
  - After an entry aborted at 0x1DE (all three vertices behind), no XGKICK
    comes between its 2-qword 1018 packet and the next entry's write to
    1019. In the interpreter's cycle count, that gap was at least 266
    cycles in the sweep. The same applies to the final kick at 0x032 and
    the next batch's first write, with the VIF upload of the next block in
    between. That these 2-qword transfers finish in time is an assumption,
    not a measurement.
  - dmem 994 (the saved loop registers) lies outside every kicked packet.
- **Not bound.** Drawing these triangles needs P1 (`em_gfx_object_unit`, GS
  class 0 state), which belongs to the binding chain.
- **GS rasterization is not checked.** No GS dump of a drawn clip triangle
  exists. The kicked words are exact; how the GS rasterizes them, including
  the zero-area collapsed triangles and the GS 16-bit X/Y of points just past
  a plane, is the renderer's.
- **Fog-off REF 2.** No captured clip unit carries it. The row itself is
  covered by C. For such a unit, the caller passes the record with dmem 1021
  replaced.
- **The TEX0_1 writes** (per entry, and the final one with a possibly stale
  dmem 1019) also change the GS TEX0_1 register. The object kernel's template
  writes TEX0 per vertex (REGS 0x4126). Whether any later draw depends on the
  leftover value was not traced.

## 7. Makefile hunk (for the chain; the Makefile is not edited here)

```make
.PHONY: test-vu1-object-clip-reference
test-vu1-object-clip-reference:
	python3 tools/test_vu1_object_clip_reference.py
```

The header needs no build-list change: it is header-only and includes only
`em_vu1_shadow_clip.h`.
