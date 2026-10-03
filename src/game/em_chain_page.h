/* em_chain_page.h - the consumer of the chain page D_007635C0 at 001D1EA0's
 * kick (docs/CHAIN_PAGE.md).
 *
 * 001CB800 (em_packet_chain_001CB800, live at every frame close) splices the
 * page's slot lists behind the start tag at base = D_0028F700 + index *
 * 0x70000 + 0x1F3EC0 (+ a1 << 6), ending in a link to base + 0x20, and the
 * DMA sends the chain to VIF1. This module does what that transfer does:
 *
 *   DMA    walks the chain from base exactly as the DMAC does in source
 *          chain mode (CNT, NEXT, REF, CALL with its two-level stack, RET;
 *          tags not transferred), until the link reaches base + 0x20 at
 *          the top level;
 *   VIF1   runs the transferred words: NOP, STCYCL, BASE, OFFSET, STMASK,
 *          STMOD 0, FLUSH / FLUSHE / FLUSHA, MPG (only the page programs'
 *          uploads and, in call mode, the grid program's, recognised by
 *          their source address), UNPACK
 *          V4-32 / V1-32 without mask, MSCAL 0 and DIRECT;
 *   VU1    MSCAL runs the loaded program's translation
 *          (em_vu1_page_programs.h: the lane program of D_00233290, the
 *          sprite program of table 0x231770, the snow program of
 *          D_00233800 and the streak program of table 0x230800) on a
 *          1024-qword data memory;
 *   GIF    DIRECT (PATH2) and each XGKICK (PATH1): PACKED tags with PRE,
 *          the registers RGBAQ, ST, XYZF2, XYZ2, TEX0_1, NOP and A+D writes
 *          of PRIM, TEX0_1, CLAMP_1, TEX1_1, ALPHA_1, COLCLAMP, TEST_1 and
 *          TEXFLUSH;
 *   GS     the vertex queue of each PRIM type under ADC, and the drawing
 *          state; every drawn primitive goes to the caller's EmGfxGsPrim
 *          array in GS order.
 * Any other tag, VIF code, program, GIF form or register faults: those are
 * all the shapes the captured pages hold (tools/test_chain_page_reference.py
 * checks every captured page against the original's own microcode).
 *
 * Top-level CALLs whose target `unit` recognises are class-2 object units
 * (001CABA0's, 001D3900 / 001D3CF0 on channel 3): the DMA would send the
 * unit's colour and node CNTs, the GS state REF of set 2 class 2, the skin
 * record, the object kernel's CALL and the model blocks (and the clip pass)
 * to VIF1, then its RET. The unit is self-contained (the kernel reads only
 * what it uploads, VU1_OBJECT_KERNEL.md section 5 C), so the caller runs it
 * (em_chain_page_live: em_object_unit_run with the class-2 GS state); its
 * primitives are drawn at that point of the page, and the GS / VIF state
 * the unit leaves (the last primitive's state, TEX0 and PRIM; CL = WL = 4)
 * holds for what follows; the VU1 data memory and program are the next
 * producer's to upload (each page producer uploads what it reads).
 * Top-level CALLs to the `skip_calls` targets are walked over without being
 * run: live, the one address 001DDE10 handed 001CB760 for slot 0xFFF this
 * frame (its four-sprite frame-copy pass, whose look the renderer does not
 * reproduce yet); they are counted, never drawn (docs/CHAIN_PAGE.md
 * section 6). The reference test also passes the captured pages' CALLs of
 * producers the port does not run.
 *
 * The VU1 registers, data memory, the VIF cycle and the GS drawing state
 * start unset at every page: the page programs read only what the page
 * uploads, and every drawing state a captured primitive uses is set earlier
 * in its page (the reference test runs every captured page from random
 * VU1 contents and gets the same primitives). The GS's internal Q (the one
 * PACKED ST holds for the next PACKED RGBAQ) is 1.0 at the start of every
 * GIF tag (measured, docs/GS_EXACT.md 2.1), so no vertex takes a Q from
 * the frame's earlier draws: every q_known is 1 (section 5 of the doc).
 *
 * List mode (em_chain_page_run_list; docs/LOAD_VEIL_PARTICLES.md section
 * 3): the same walk over a whole frame list that ends in an END tag (step
 * V's main list: 001D21E0 closes it with a NEXT to the GS block's END at
 * D_00275674 + 0x10), with the GS environment registers a frame list sends
 * as well (FRAME_1, ZBUF_1, XYOFFSET_1, SCISSOR_1, PRMODECONT, DTHE, FBA_1,
 * PABE, TEXA, SCANMSK, FOGCOL; the context-2 set of the draw environments,
 * which no context-1 primitive reads) and A+D vertex registers (RGBAQ with
 * its Q, ST, UV, XYZF2, XYZ2). Each primitive then also gets the context-1
 * environment in force (prim_env). The page never holds any of these, so
 * page mode still faults on them.
 *
 * Fail-stop: the first fault latches (fault, fault_address) and returns -1;
 * the caller reports it. */
#ifndef EM_CHAIN_PAGE_H
#define EM_CHAIN_PAGE_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_vu1_page_programs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_CHAIN_PAGE_ARENA     0x0028F700u   /* D_0028F700 */
#define EM_CHAIN_PAGE_START_OFF 0x001F3EC0u   /* 001CB800's base offset */
#define EM_CHAIN_PAGE_BUFFER    0x00070000u   /* per D_00810E80 index    */
#define EM_CHAIN_PAGE_LANE      0x00233290u   /* D_00233290              */
#define EM_CHAIN_PAGE_SPRITE    0x00231770u   /* table 0x231770          */
#define EM_CHAIN_PAGE_SNOW      0x00233800u   /* D_00233800              */
#define EM_CHAIN_PAGE_STREAK    0x00230800u   /* table 0x230800          */
#define EM_CHAIN_PAGE_KIND2     0x00232540u   /* table 0x232540          */
#define EM_CHAIN_PAGE_GRID      0x0023C990u   /* packet 0x23C990 (call mode) */

enum {
    EM_CHAIN_PAGE_OK = 0,
    EM_CHAIN_PAGE_FAULT_ARGS,      /* NULL input or output                  */
    EM_CHAIN_PAGE_FAULT_READ,      /* an address the reader does not map     */
    EM_CHAIN_PAGE_FAULT_DMA,       /* a tag ID / bit / nesting not in pages  */
    EM_CHAIN_PAGE_FAULT_VIF,       /* a VIF code or form not in pages        */
    EM_CHAIN_PAGE_FAULT_PROGRAM,   /* an MPG / MSCAL of an unknown program   */
    EM_CHAIN_PAGE_FAULT_VU,        /* the program translation faulted        */
    EM_CHAIN_PAGE_FAULT_GIF,       /* a GIF form or register not in pages     */
    EM_CHAIN_PAGE_FAULT_CAPACITY   /* more primitives or transfers than room  */
};

#define EM_CHAIN_PAGE_SKIP_MAX 64u
#define EM_CHAIN_PAGE_UNITS_MAX 32u

/* Original memory, by address: `size` bytes at `address`, or NULL. */
typedef const uint8_t *(*EmChainPageRead)(void *ctx, uint32_t address, uint32_t size);
/* A top-level CALL's target that is a class-2 object unit (001CABA0's on
 * channel 3, CALLed at its depth by 001CAAC0 / 001CB760): the triangles
 * the unit's programs draw, as GS primitives with the unit's GS state, into
 * out[0 .. capacity) (*count of them). 1: a unit; 0: not one (the CALL is
 * walked); -1: a fault. */
typedef int (*EmChainPageUnit)(void *ctx, uint32_t address, EmGfxGsPrim *out, uint32_t capacity, uint32_t *count);

typedef struct {
    uint32_t transfers;       /* DMA tags walked                          */
    uint32_t qwords;          /* qwords transferred                       */
    uint32_t direct;          /* DIRECT packets (PATH2)                   */
    uint32_t mscal_lane, mscal_sprite;
    uint32_t kicks;           /* XGKICKs (PATH1)                          */
    uint32_t prims;           /* primitives drawn                         */
    uint32_t prim_type[8];    /* by PRIM type                             */
    uint32_t skipped;         /* top-level CALLs to skip_calls walked over */
    uint32_t stale_q;         /* vertices with q_known 0 (none since the per-tag Q) */
    uint32_t cycle_inherited; /* UNPACKs before the page's first STCYCL      */
    uint32_t mscal_snow;      /* MSCALs of the snow program (D_00233800)     */
    uint32_t units;           /* class-2 object units drawn (unit CALLs)     */
    uint32_t mscal_streak;    /* MSCALs of the streak program (0x230800)     */
    uint32_t streak_prims;    /* of the primitives, the streak program's     */
    uint32_t mscal_kind2;     /* MSCALs of the kind-2 program (0x232540)     */
    uint32_t kind2_prims;     /* of the primitives, the kind-2 program's     */
    uint32_t lane_strips;     /* of the primitives, the lane program's strip
                               * triangles (an active ring-decal slot of
                               * 001F0460's, drawn by 001F0720's lanes)       */
    uint32_t direct_strips;   /* of the primitives, DIRECT packets' strip
                               * triangles (PRIM type 4)                     */
    uint32_t mscal_grid;      /* MSCALs of the grid program (0x23C990)       */
} EmChainPageCounts;

/* One vertex's Q provenance, parallel to EmGfxGsPrim.v (1: the Q of its
 * GIF tag, 1.0 or a PACKED ST's, or an A+D RGBAQ's; always 1 since the
 * per-tag Q). */
typedef struct {
    uint8_t q_known[3];
} EmChainPageQ;

typedef struct {
    /* input */
    EmChainPageRead read;
    void *read_ctx;
    const uint32_t *skip_calls; /* CALL targets walked over (top level)  */
    uint32_t skip_count;
    EmChainPageUnit unit;     /* may be NULL: no unit CALLs                 */
    void *unit_ctx;
    /* output (caller-owned arrays of prim_capacity) */
    EmGfxGsPrim *prims;
    EmChainPageQ *prim_q;     /* may be NULL */
    EmGfxGsEnv *prim_env;     /* list mode: may be NULL (page mode: unused) */
    uint32_t prim_capacity;
    uint32_t prim_count;
    EmChainPageCounts counts;
    /* the unit CALLs drawn (their targets) and the primitives each drew:
     * [unit_first[k], unit_first[k] + unit_prims[k]) of prims */
    uint32_t unit_call[EM_CHAIN_PAGE_UNITS_MAX], unit_first[EM_CHAIN_PAGE_UNITS_MAX];
    uint32_t unit_prims[EM_CHAIN_PAGE_UNITS_MAX];
    uint64_t unit_tex0[EM_CHAIN_PAGE_UNITS_MAX];   /* the TEX0 / PRIM a unit leaves */
    uint32_t unit_prim[EM_CHAIN_PAGE_UNITS_MAX];
    uint32_t fault;           /* EM_CHAIN_PAGE_FAULT_*                    */
    uint32_t fault_address;   /* the tag, VIF code or GIF tag address     */
    uint32_t fault_detail;    /* the offending word / program / register  */
    /* machine (reset at every run) */
    EmVu1PRegs regs;
    EmVu1PQword dmem[EM_VU1P_DMEM_QWORDS];
} EmChainPage;

/* 001CB800's start tag of `index` (D_00810E80) and a1. */
static inline uint32_t em_chain_page_start(int32_t index, int32_t a1)
{
    return EM_CHAIN_PAGE_ARENA + (uint32_t)index * EM_CHAIN_PAGE_BUFFER + EM_CHAIN_PAGE_START_OFF +
           ((uint32_t)a1 << 6);
}

/* Walk the page at `start` and fill p->prims. 0, or -1 (p->fault). */
int em_chain_page_run(EmChainPage *p, uint32_t start);

/* List mode: walk the frame list at `start` up to its top-level END tag
 * (transferred) and fill p->prims (and p->prim_env). 0, or -1. */
int em_chain_page_run_list(EmChainPage *p, uint32_t start);

/* Call mode: the list mode walk of a channel list a frame list CALLs (the
 * channel-3 background list at context +0x1D8, 001E1E60's), from `start`
 * up to its own top-level RET (transferred), with the grid program 0x23C990
 * (its MPG of 79 instructions from ELF 0x0023C9B8, em_vu1_grid_program_mscal)
 * among the programs; the RET returns to the caller's list, so the walk
 * stops there. An END faults. 0, or -1. */
int em_chain_page_run_call(EmChainPage *p, uint32_t start);

/* A short name of a fault code, for reports. */
const char *em_chain_page_fault_name(uint32_t fault);

#ifdef __cplusplus
}
#endif

#endif
