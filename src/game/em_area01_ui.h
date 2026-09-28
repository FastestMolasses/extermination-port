/* AREA01 side track, lane UI: the side-beat-only boot functions of the
 * census a01 delta whose subsystem labels are ui_screens, ui_credits and
 * draw2d (labels only; the roles below come from the code and the
 * captures). Docs: docs/AREA01_UI.md. Nothing here is wired.
 *
 * Hand translations of the original functions (boot ELF SCUS-97112):
 *
 *   status pages (the status block t = D_00810130 is their argument)
 *     0020F950(t)       page 1 of the status screen (00214020 is page 3):
 *                       t[3] = 0 allocates 22 pool nodes running 002101C0
 *                       and picks the cursor; 1 and 2 draw and read the pad
 *     002101C0(p)       the pool callback those nodes run
 *     00210030(p, a1)   node +0xB0..B8 from the camera block 0x810610..3C
 *     00210A00(a0)      background blits (00207E40 through 00207D00 modes)
 *     00210C00(a0)      pad-arrow blits chosen by D_00810E70
 *     00210F30(t)       the marker triangle (00208040) of page 1
 *     00211400(t, a1)   page 1's item markers (00211240 / 002117D0 + 00211310)
 *     00214020(t)       page 3 of the status screen: a five-state machine
 *     002131B0(p, a1)   page 3 frame blits (and 001FCF30 when a1 == 0)
 *     002134C0(p, a1)   page 3 arrows (a1 == 0) or page numbers (a1 != 0)
 *     00213F30(p,lo,hi) page 3's list of ids for the category p[0x12]
 *     00207D90(slot, b0, b2, b1, b3)   one 0x40-byte packet on the slot
 *     00208040(slot, a, b, c, rgba)    one 0x60-byte packet on the slot
 *   the effect driver of the a01_s3 burn (a pool callback)
 *     0022BBC0(seq)     timeline, interpolators, per-bone emitters, ring
 *     0022B7A0(e)       picks the timeline table (byte-matched C)
 *     0022B700(p, n)    bone slots (byte-matched C)
 *     0022BB70(i)       ring slot address (byte-matched C)
 *
 * Memory: every original byte is reached through the views of
 * EmArea01RenderCore (em_area01_render_mem.h) by its original address: EE
 * RAM, the scratchpad at 0x70000000, and (for the stack buffers 002134C0
 * and 0022BBC0 hand to their callees) the EE stack. The routines carve
 * their frames below `sp` exactly as the original does (frame sizes from
 * the instructions), so a stack address a callee receives is the
 * original's address.
 *
 * Callees: every function outside this lane is reached through `call`, by
 * its original address, with the integer argument registers a0..a3, t0..t2
 * as full 64-bit images (a 32-bit int is passed sign-extended, as the EE
 * holds it) and the float argument registers f12.. as binary32 bits; v0 and
 * f0 come back. `sp` is the caller's stack pointer at the call. 00102948
 * and 00102958 (quadword copies) run inline. An indirect call (002101C0's
 * node +0x4C) passes the loaded function pointer as the target.
 *
 * Fail-stop (the math lane's convention): the first fault is latched in
 * core.fault (address = the worker for codes 1/2, the running routine for
 * 4/6). After it no store is made, no callee is called and loads read 0;
 * every entry returns -1. Codes: 1 `call` is NULL, 2 a callee returned a
 * negative value, 4 an address no view covers, 6 the original would read
 * a register the caller never set (docs/AREA01_UI.md section 2: 00213F30's
 * lo/hi from 00214020 with t[0x12] outside 0..4, 0022BBC0's period with
 * seq[0xD] >= 10 and its burst before any kind set it), or an integer
 * division by zero (0022BBC0 timeline op 1).
 *
 * Arithmetic: every COP1 operation goes through em_ee_float.h on binary32
 * bit patterns. stdint only. */
#ifndef EM_AREA01_UI_H
#define EM_AREA01_UI_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One callee call; returns >= 0, or a negative value (latched as code 2). */
typedef int (*EmArea01UiCall)(void *ctx, uint32_t target, uint32_t sp, const uint64_t *a, unsigned na,
                              const uint32_t *f, unsigned nf, uint64_t *v0, uint32_t *f0);

typedef struct {
    EmArea01RenderCore core; /* views, the fault latch, the running routine */
    EmArea01UiCall call;
    void *ctx;
    uint32_t sp; /* the EE stack pointer at entry */
    /* Nonzero: 0020F950 and 00210F30 reach 00207D90 and 00208040 through
     * `call` (by original address, the register images the original passes)
     * instead of running their translations here, for a binder whose 2D
     * layer consumes the calls rather than the render context's packet
     * arena (em_status_pages_live). 0 (the lane's tests): the translations
     * write the packets, as the original does inside those calls. */
    uint32_t leaf_calls;
} EmArea01Ui;

/* All entries: 0 on success, -1 on a fault (latched). */
int em_area01_ui_00207D90(EmArea01Ui *s, int32_t slot, int32_t b0, int32_t b2, int32_t b1, int32_t b3);
int em_area01_ui_00208040(EmArea01Ui *s, int32_t slot, uint32_t a1, uint32_t a2, uint32_t a3, uint64_t a4);
int em_area01_ui_0020F950(EmArea01Ui *s, uint32_t t);
int em_area01_ui_00210030(EmArea01Ui *s, uint32_t p, int32_t a1);
int em_area01_ui_002101C0(EmArea01Ui *s, uint32_t p);
int em_area01_ui_00210A00(EmArea01Ui *s, int32_t a0);
int em_area01_ui_00210C00(EmArea01Ui *s, int32_t a0);
int em_area01_ui_00210F30(EmArea01Ui *s, uint32_t t);
int em_area01_ui_00211400(EmArea01Ui *s, uint32_t t, int32_t a1);
int em_area01_ui_002131B0(EmArea01Ui *s, uint32_t p, int32_t a1);
int em_area01_ui_002134C0(EmArea01Ui *s, uint32_t p, int32_t a1);
int em_area01_ui_00213F30(EmArea01Ui *s, uint32_t p, int32_t lo, int32_t hi);
int em_area01_ui_00214020(EmArea01Ui *s, uint32_t t);
int em_area01_ui_0022B700(EmArea01Ui *s, uint32_t p, uint32_t n, uint32_t *v0);
int em_area01_ui_0022B7A0(EmArea01Ui *s, uint32_t e, uint32_t *v0);
int em_area01_ui_0022BB70(EmArea01Ui *s, int32_t a0, uint32_t *v0);
int em_area01_ui_0022BBC0(EmArea01Ui *s, uint32_t seq);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_UI_H */
