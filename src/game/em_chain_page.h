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
 *          STMOD 0, FLUSH / FLUSHE / FLUSHA, MPG (only the two page
 *          programs' uploads, recognised by their source address), UNPACK
 *          V4-32 / V1-32 without mask, MSCAL 0 and DIRECT;
 *   VU1    MSCAL runs the loaded program's translation
 *          (em_vu1_page_programs.h: the lane program of D_00233290 and the
 *          sprite program of table 0x231770) on a 1024-qword data memory;
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
 * PACKED ST holds for the next RGBAQ) is the exception: a sprite whose
 * RGBAQ precedes every ST of the page takes the frame's Q, which the port
 * does not model; such vertices carry q_known 0 (section 5 of the doc).
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

/* Original memory, by address: `size` bytes at `address`, or NULL. */
typedef const uint8_t *(*EmChainPageRead)(void *ctx, uint32_t address, uint32_t size);

typedef struct {
    uint32_t transfers;       /* DMA tags walked                          */
    uint32_t qwords;          /* qwords transferred                       */
    uint32_t direct;          /* DIRECT packets (PATH2)                   */
    uint32_t mscal_lane, mscal_sprite;
    uint32_t kicks;           /* XGKICKs (PATH1)                          */
    uint32_t prims;           /* primitives drawn                         */
    uint32_t prim_type[8];    /* by PRIM type                             */
    uint32_t skipped;         /* top-level CALLs to skip_calls walked over */
    uint32_t stale_q;         /* vertices whose Q is the frame's (q_known 0) */
    uint32_t cycle_inherited; /* UNPACKs before the page's first STCYCL      */
} EmChainPageCounts;

/* One vertex's Q provenance, parallel to EmGfxGsPrim.v (1: set by a PACKED
 * ST or an A+D RGBAQ of this page). */
typedef struct {
    uint8_t q_known[3];
} EmChainPageQ;

typedef struct {
    /* input */
    EmChainPageRead read;
    void *read_ctx;
    const uint32_t *skip_calls; /* CALL targets walked over (top level)  */
    uint32_t skip_count;
    /* output (caller-owned arrays of prim_capacity) */
    EmGfxGsPrim *prims;
    EmChainPageQ *prim_q;     /* may be NULL */
    EmGfxGsEnv *prim_env;     /* list mode: may be NULL (page mode: unused) */
    uint32_t prim_capacity;
    uint32_t prim_count;
    EmChainPageCounts counts;
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

/* A short name of a fault code, for reports. */
const char *em_chain_page_fault_name(uint32_t fault);

#ifdef __cplusplus
}
#endif

#endif
