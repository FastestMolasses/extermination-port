/* em_object_unit.h - one 001CA990 draw unit, from its DMA bytes to the GS
 * triangles the original VU1 programs kick. Docs: docs/OWNER_DRAW.md
 * sections 3, 6 and 7 (P1/P2).
 *
 * em_object_unit_parse reads the unit exactly as the DMAC and VIF1 would:
 * the tags 001C7420, 001D1F80(0, 1, 0) and 001CA940 wrote (the colour CNT,
 * the node CNTs, the REF 9 GS state (its address: set 1 class 0), the skin
 * record REF, the arena REF 1, the CALL, the optional fog-off REF 2 and the
 * model REF, then the same for the clip pass when 001CA7B0's flags had bit
 * 0 set), or a face unit as 001C7900, 001CB2C0, 001D1F80 and 001D3E40
 * write it (the colour CNT, one node CNT, the weights CNT, the GS state,
 * the arena REF, the CALL 0x0023C480, the skin record, the face blocks;
 * VU1_FACE_MORPH.md section 2). A REF target is read
 * through the caller's resolver by its original address. Every VIF code and
 * GS register value the unit carries is checked against the one form the
 * port reproduces; anything else is refused (the reason is returned), never
 * guessed. The result is an EmGfxObjectUnit (em_gfx.h) whose views point
 * into the pieces' own copies and the resolved model blocks.
 *
 * em_object_unit_run is what the VU1 does with those pieces: a data-memory
 * image per program (the unit's uploads, every other qword zero: the unit is
 * self-contained, docs/VU1_OBJECT_KERNEL.md section 5 C and
 * docs/VU1_OBJECT_CLIP.md section 5 A), the object kernel 0x0023C750 over
 * every model block (em_vu1_object_kernel.h: MSCAL for block 0, MSCNT
 * after, the double-buffer TOP rule), and, for a clip unit, the clip
 * program 0x002354A0 over the same blocks after it (em_vu1_object_clip.h);
 * for a face unit the face morph program 0x0023C480 over its blocks
 * (em_vu1_face_morph.h). Its output is every drawn triangle in GS terms, in
 * the order the GS receives them: the object (or face) pass block by block
 * in strip order, then the clip pass block by block in kick order.
 *
 * Checked by tools/test_object_unit_reference.py (the captured owner units
 * through the original VU1 microcode) and tests/object_unit_test.c. Pure C,
 * no GPU. */
#ifndef EM_OBJECT_UNIT_H
#define EM_OBJECT_UNIT_H

#include <stddef.h>
#include <stdint.h>

#include "em_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_OBJECT_UNIT_KERNEL      UINT32_C(0x0023C750)  /* 001D37D0's CALL */
#define EM_OBJECT_UNIT_CLIP_KERNEL UINT32_C(0x002354A0)  /* 001D3AD0's CALL */
#define EM_OBJECT_UNIT_FOG_OFF     UINT32_C(0x002514B0)  /* the optional REF 2 */
#define EM_OBJECT_UNIT_FACE_KERNEL UINT32_C(0x0023C480)  /* 001D3E40's CALL */
#define EM_OBJECT_UNIT_GS_STATE    UINT32_C(0x00815360)  /* 001D1F80(0, 1, 0): set 1, class 0 */
#define EM_OBJECT_UNIT_GS_STATE_2  UINT32_C(0x00815A20)  /* 001D1F80(3, 2, 2): set 2, class 2 */
#define EM_OBJECT_UNIT_SKIN_3      UINT32_C(0x00816B40)  /* 001D3900's skin record (+0x80 per slot) */
#define EM_OBJECT_UNIT_SKIN_3_CLIP UINT32_C(0x00816E40)  /* 001D3CF0's */
#define EM_OBJECT_UNIT_ARENA       UINT32_C(0x00814220)  /* *D_00275674: the arena's first qword */
/* 001C7420 writes at most 0x1B0 / 8 = 54 nodes before the rows would reach
 * the kernel's first batch buffer (dmem 0x1B0); the unit is refused above. */
#define EM_OBJECT_UNIT_MAX_NODES 54u
#define EM_OBJECT_UNIT_BLOCK_QWORDS 0x82u
#define EM_OBJECT_UNIT_FACE_BLOCK_QWORDS 0x163u   /* 3 VIF-code qwords + 32 x 11 */
#define EM_OBJECT_UNIT_TEMPLATE_PRIM 0x03Cu   /* the kernel's GIF template (dmem 1020) */
#define EM_OBJECT_UNIT_CLIP_PRIM 0x03Bu       /* skin record 1's triangle-list tag */
#define EM_OBJECT_UNIT_ABE 0x040u             /* the channel-3 skin records' tags add ABE */

/* A REF target by original address: `bytes` bytes, or NULL (refused). */
typedef const uint8_t *(*EmObjectUnitResolve)(void *ctx, uint32_t address, uint32_t bytes);

typedef struct {
    EmGfxObjectUnit unit;                     /* views into the fields below */
    uint32_t color[16];
    uint32_t nodes[32u * EM_OBJECT_UNIT_MAX_NODES];
    uint32_t weights[8];                      /* face: dmem 1011..1012 */
    uint32_t constants[28];
    uint32_t clip_constants[28];
    uint32_t model_address;                   /* the model REF target (model + 0x40) */
    uint32_t skin_address[2];                 /* the skin record REF targets */
    uint32_t fog_off;                         /* the REF 2 was emitted (bit 0: pass 0, bit 1: clip) */
    uint32_t bytes;                           /* unit bytes consumed */
} EmObjectUnitPieces;

/* A unit without a GS state REF of its own (001F3E30's 001C7900 +
 * 001CA940): parsed as em_object_unit_parse with the REF optional; the
 * caller establishes that the channel's state in force is class 0. */
int em_object_unit_parse_inherit(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                                 EmObjectUnitPieces *out, const char **why);
/* Parse the unit at `unit` (`size` bytes, the whole unit: parsing must end
 * exactly at `size`). 0, or -1 with *why set to a static reason. */
int em_object_unit_parse(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                         EmObjectUnitPieces *out, const char **why);
/* 001F5F60's exact colour/node VIF ordering, followed by the shared class-2
 * object submit. One node, no clip pass. Other parser entries stay strict. */
int em_object_unit_parse_light(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                               EmObjectUnitPieces *out, const char **why);
/* The same for the first unit of a sequence (001CAA00 appends 001CB3C0's
 * face unit after the owner's own): out->bytes is where it ended. */
int em_object_unit_parse_one(const uint8_t *unit, uint32_t size, EmObjectUnitResolve resolve, void *ctx,
                             EmObjectUnitPieces *out, const char **why);

/* The bytes of the GS state REF (9 qwords): 0 when they are exactly the
 * class-0 set the backend reproduces (FLUSH, DIRECT 8, seven PACKED A+D
 * writes: TEX1_1 0x60, TEST_1 0x5000D, ZBUF_1 with ZMSK 0, ALPHA_1
 * 0x80000000A8, CLAMP_1 0, COLCLAMP 1 and a PRIM the kernel's PRE template
 * overrides), else -1 with *why. The parser checks the REF's address;
 * the reference test checks these bytes in every capture. */
int em_object_unit_gs_state_check(const uint8_t *bytes, const char **why);
/* The same for 001D1F80(3, 2, 2)'s REF (set 2, class 2): TEX1_1 0x60, TEST_1
 * 0x53001 (alpha test NEVER with AFAIL RGB_ONLY: no Z or alpha write),
 * ZBUF_1 with ZMSK 1, ALPHA_1 0x8000000068 ((Cs - 0) * FIX 0x80 >> 7 + Cd),
 * CLAMP_1 0, COLCLAMP 1 and a PRIM the template overrides; the bytes of
 * every AREA11 capture (tools/test_object_unit_reference.py). */
int em_object_unit_gs_state_check_2(const uint8_t *bytes, const char **why);
/* The GS register values of class `gs_class` (0 or 2) the page path draws
 * with (EM_GFX_GS_* of em_gfx.h): alpha, test, tex1, clamp, colclamp. */
void em_object_unit_gs_state(uint32_t gs_class, uint64_t *alpha, uint64_t *test, uint64_t *tex1,
                             uint64_t *clamp, uint64_t *colclamp);

/* One kicked vertex, as the GS takes it. */
typedef struct {
    uint16_t x, y;       /* XYZF2 X, Y: GS 12.4 */
    uint32_t z;          /* XYZF2 Z: 24 bits */
    uint8_t f;           /* XYZF2 F */
    uint8_t rgba[4];     /* RGBAQ: each lane's low byte */
    uint32_t s, t, q;    /* ST S, T and RGBAQ Q (binary32 bit patterns) */
} EmObjectUnitVertex;

typedef struct {
    uint64_t tex0;       /* TEX0_1 in force for the triangle (CLD included) */
    uint16_t pass;       /* 0: the object kernel or the face program, 1: the clip program */
    uint16_t block;      /* the model block */
    EmObjectUnitVertex v[3];
} EmObjectUnitTriangle;

typedef struct {
    EmObjectUnitTriangle *tri;   /* grown with realloc; free with em_object_unit_result_free */
    uint32_t count, capacity;
    uint32_t object_triangles;   /* the first `object_triangles` come from pass 0 */
    uint32_t prim;               /* pass 0's PRIM (the template's), pass 1's is prim - 1 */
    const char *why;             /* reason of the last -1 */
    uint32_t why_block;
} EmObjectUnitResult;

/* Run the unit's programs. 0 (r->count triangles), or -1 with r->why. The
 * result's storage is reused across calls. */
int em_object_unit_run(const EmGfxObjectUnit *u, EmObjectUnitResult *r);
void em_object_unit_result_free(EmObjectUnitResult *r);

#ifdef __cplusplus
}
#endif

#endif /* EM_OBJECT_UNIT_H */
