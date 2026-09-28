/* em_static_world_draw.h - the static world's channel-0 run, as the DMA
 * sends it to VU1 and the GS (docs/STATIC_WORLD.md section 7).
 *
 * 001C1D00 -> 001D5370 writes the run at the channel-0 cursor every world
 * frame (em_static_world: 001D4DA0, 001D4FB0, 001D4B20). Step V's main list
 * sends channel 0 after the channel-3 background, and the DMAC walks the run
 * in source-chain mode with tag transfer off. This module does what that
 * transfer does, for the shapes the run holds and nothing else:
 *
 *   DMA    CNT (the data after the tag), REF (the data at the address) and
 *          CALL to one of the two kernel packets (qwc 0); every other tag
 *          faults. The upper eight bytes of a tag are never sent (the
 *          builders leave stale bytes there).
 *   CALL   0x00237180 (the level kernel) or 0x00239C90 (the guard-band
 *          clip kernel): the packet's VIF codes, which the reference test
 *          reads from the ELF and asserts: STCYCL 4,4, BASE 0x190, OFFSET
 *          0x109 / 0x101 (the double buffer back to BASE) and the MPG of
 *          the program. No microcode is transferred: the program's
 *          translation runs instead (em_vu1_level_kernel.h,
 *          em_vu1_shadow_clip.h).
 *   VIF1   NOP, STCYCL, FLUSH / FLUSHE / FLUSHA, UNPACK V4-32 without mask
 *          (to an address, or with FLG to TOPS), MSCAL 0, MSCNT and DIRECT.
 *          The run's first UNPACK comes before any STCYCL of the run: the
 *          cycle is the one the list left, which must write contiguously
 *          (CL == WL); `cl` / `wl` hold it (the channel-3 background's
 *          001D7100 leaves 4, 4).
 *   VU1    MSCAL / MSCNT run the loaded kernel over the batch at TOP (the
 *          double buffer alternates BASE, BASE + OFFSET from the CALL on) on
 *          one persistent 1024-qword data memory; each XGKICK's packet goes
 *          to the GIF.
 *   GIF    DIRECT (the GS state REF of 001D1F80(0, 1, 0): A+D writes) and
 *          the kicked PACKED packets (the level kernel's strip template,
 *          the clip kernel's one-register TEX0 tag and triangle list).
 *   GS     the drawing state (PRIM, TEX0_1, TEX1_1, TEST_1, ALPHA_1,
 *          CLAMP_1, ZBUF_1, COLCLAMP) and the vertex queue of PRIM types 3
 *          (triangle list) and 4 (strip) under ADC; every drawn triangle
 *          goes to `prims` in GS order, with the state in force.
 *
 * Every GS state REF must be the class-0 set em_object_unit_gs_state_check
 * accepts (the one set the renderer reproduces; every captured run has
 * only that one).
 *
 * Fail-stop: the first fault latches (fault, fault_address, fault_detail);
 * the run returns -1 and the caller reports it. */
#ifndef EM_STATIC_WORLD_DRAW_H
#define EM_STATIC_WORLD_DRAW_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_vu1_level_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EM_SWD_LEVEL_KERNEL 0x00237180u   /* 001D4DA0's CALL target */
#define EM_SWD_CLIP_KERNEL  0x00239C90u   /* 001D4960's CALL target */
#define EM_SWD_CLIP_OFFSET  0x101u        /* 0x00239C90's VIF OFFSET */
#define EM_SWD_GS_STATE     0x00815360u   /* 001D1F80(0, 1, 0): set 1, class 0 */

enum {
    EM_SWD_OK = 0,
    EM_SWD_FAULT_ARGS,        /* NULL input                                 */
    EM_SWD_FAULT_READ,        /* an address the reader does not map         */
    EM_SWD_FAULT_DMA,         /* a tag ID or form not in the run            */
    EM_SWD_FAULT_VIF,         /* a VIF code or form not in the run          */
    EM_SWD_FAULT_PROGRAM,     /* a CALL target or MSCAL of no known program */
    EM_SWD_FAULT_VU,          /* a kernel translation faulted               */
    EM_SWD_FAULT_GIF,         /* a GIF form or register not in the run      */
    EM_SWD_FAULT_GS_STATE,    /* a GS state REF other than the class-0 set  */
    EM_SWD_FAULT_CAPACITY     /* out of memory for the triangles            */
};

/* Original memory, by address: `size` bytes at `address`, or NULL. */
typedef const uint8_t *(*EmSwdRead)(void *ctx, uint32_t address, uint32_t size);

/* A test hook: every XGKICK, with its program (the CALL target), the TOP
 * of its batch and the kicked packet's qwords up to its EOP tag. */
typedef void (*EmSwdKick)(void *ctx, uint32_t program, uint32_t top, const uint32_t *qwords,
                          uint32_t qword_count);

typedef struct {
    uint32_t tags;            /* DMA tags walked                             */
    uint32_t calls[2];        /* CALLs of the level / the clip kernel        */
    uint32_t batches[2];      /* MSCAL + MSCNT of the level / the clip kernel */
    uint32_t kicks;           /* XGKICKs                                     */
    uint32_t triangles[2];    /* triangles drawn from the level / clip kicks */
    uint32_t culled;          /* level vertices whose only ADC is the cull   */
    uint32_t gs_states;       /* GS state REFs                               */
} EmSwdCounts;

typedef struct {
    /* input */
    EmSwdRead read;
    void *read_ctx;
    EmSwdKick kick;           /* NULL in the game */
    void *kick_ctx;
    uint8_t cl, wl;           /* the VIF cycle the list left before the run  */
    /* output: grown with realloc, freed by em_static_world_draw_free */
    EmGfxGsPrim *prims;
    uint32_t prim_count, prim_capacity;
    EmSwdCounts counts;
    uint32_t fault;           /* EM_SWD_FAULT_*                              */
    uint32_t fault_address;   /* the tag, VIF code or GIF tag address        */
    uint32_t fault_detail;    /* the offending word / program / register     */
    /* machine (the data memory and the level kernel's carried registers
     * persist across runs, as VU1's do; everything else restarts) */
    EmVu1ObjQword dmem[1024];
    EmVu1LvlState level;
} EmStaticWorldDraw;

/* Walk the run [start, end) (the channel-0 bytes 001C1D00 wrote this frame)
 * and fill d->prims. 0, or -1 (d->fault). */
int em_static_world_draw_run(EmStaticWorldDraw *d, uint32_t start, uint32_t end);

void em_static_world_draw_free(EmStaticWorldDraw *d);

const char *em_static_world_draw_fault_name(uint32_t fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATIC_WORLD_DRAW_H */
