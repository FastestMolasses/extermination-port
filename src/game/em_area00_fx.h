/* AREA00 side track, lane A00FX: the AREA00 new functions of the census a00
 * delta (../Extermination/build/s87/census/a00_delta.json) whose subsystem
 * labels are fx_render, gs_upload, render_vif, frame_update and frame_main.
 * The labels are the census's; the roles below come from the code and the
 * captures. Docs: docs/AREA00_FX.md. Nothing here is wired.
 *
 * Hand translations of the original functions (boot ELF SCUS-97112). Each
 * follows the instructions, not the decomp's NEARMISS C, where they differ
 * (docs/AREA00_FX.md section 2 lists the differences):
 *
 *   packets (em_area00_fx_gs.c)
 *     001CD940(mode, p0, c0, p1, c1)  one fogged line: both ends clip-tested
 *                     and projected, colours staged at 0x70003620/30, the
 *                     four quadwords appended at 001CB5F0's slot
 *     001D6DD0(ch, a1, a2)    one A+D register write (reg 0x18) on the
 *                     channel ch cursor; returns the body address
 *     001D7510(ch, a1, a2)    a five-register GS state block on channel ch
 *     001D7A80(ch, a1, a2, a3, t0)  a GIF block with five copied quadwords
 *     001F4A00(a0, a1)        tail call of 001F4BF0
 *     001F4E20(a0, a1, f12)   tail call of 001F4D40 with f13 = 5.5
 *   spawn wrappers and the subtype-6 handler (em_area00_fx_spawn.c)
 *     001AA840()              the first list node matching (+0 == 1,
 *                     +2 & 0x1F == 4, +3 == 0x29) goes to 001AA7A0
 *     001EF510(a0, a1)        the 001EA240 handler of effect subtype 6
 *     001EFF10 / 001EFFD0 / 001F0060 / 001F00A0   001EF9D0 wrappers
 *     001F02C0(p, id, f12)    a sound at a point (001FBD50 on stack buffers)
 *   the trail effect 0x8000000D (em_area00_fx_trail.c)
 *     001F18C0(node)          its node callback; 001F1550 (bone slots) and
 *                     001F15F0 (the strip packet) are its helpers
 *   the debris effects (em_area00_fx_debris.c)
 *     001F2BA0(node)          node callback of effect ids 0x8000000A..C,
 *                     0x21, 0x31, 0x32, 0x36, 0x4C, 0x4D, 0x5E, 0x6E;
 *                     001F2E90 / 001F2F90 seed the pieces, 001F3620 moves
 *                     one, 001F3340 sweeps it, 001F3E30 draws it
 *   the exit-phase effect (em_area00_fx_exit.c)
 *     001F4F40(sub)           allocates a node running 001F5040
 *     001F5040(node)          its callback; 001F4F90 draws its eight lines
 *     001F5490(p), 001F5F60(pos, rot, a2, entry)   001C50B0's helpers
 *   the glow effect 0x8000000F (em_area00_fx_glow.c)
 *     001F6FB0(node)          node callback (the parent is node +0x24)
 *
 * Memory: every original byte is reached through the views of
 * EmArea01RenderCore (em_area01_render_mem.h) by its original address: EE
 * RAM, the scratchpad at 0x70000000 and the EE stack. Each routine carves its
 * frame below `sp` with the original's frame size, so a stack address a
 * callee receives is the original's address. Register saves are not
 * stored (docs/AREA00_FX.md section 5).
 *
 * Callees: every function outside this lane is reached through `call`, by
 * its original address, with the registers the original sets up or the
 * callee reads (EmArea00FxRegs: a mask and 64-bit images of the GPRs, a mask
 * and binary32 bits of the FPRs); v0 (64-bit) and f0 come back. `sp` is the
 * caller's stack pointer at the call. 00102948 and 00102958 run inline. An
 * indirect call (001F5040's node +0x4C) passes the loaded pointer as the
 * target. Lane routines call each other directly.
 *
 * Arguments are the original's register images (a 32-bit value the caller
 * loaded with lw is sign-extended). Where the original compares or moves a
 * whole register, the translation does too.
 *
 * Fail-stop (the UI lane's convention): the first fault is latched in
 * core.fault (address = the worker for codes 1/2, the running routine for
 * 4/6). After it no store is made, no callee is called and loads read 0;
 * every entry then returns -1. Codes: 1 `call` is NULL, 2 a callee returned
 * a negative value, 4 an address no view covers, 6 a VU form or a clip lane
 * outside the measured model (em_ee_float.h), or an integer division by
 * zero the EE result of which is not modelled.
 *
 * Arithmetic: every COP1 and VU0 operation goes through em_ee_float.h on
 * binary32 bit patterns. stdint only. */
#ifndef EM_AREA00_FX_H
#define EM_AREA00_FX_H

#include <stdint.h>

#include "game/em_area01_render_mem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The registers one callee call passes. Bit n of imask: GPR n is passed
 * (r[n] its 64-bit image); bit n of fmask: FPR n (f[n] its bits). */
typedef struct {
    uint32_t imask;
    uint32_t fmask;
    uint64_t r[32];
    uint32_t f[32];
} EmArea00FxRegs;

/* One callee call; returns >= 0, or a negative value (latched as code 2). */
typedef int (*EmArea00FxCall)(void *ctx, uint32_t target, uint32_t sp, const EmArea00FxRegs *in, uint64_t *v0,
                              uint32_t *f0);

typedef struct {
    EmArea01RenderCore core; /* views, the fault latch, the running routine */
    EmArea00FxCall call;
    void *ctx;
    uint32_t sp; /* the EE stack pointer at entry */
} EmArea00Fx;

/* All entries: 0 on success, -1 on a fault (latched). v0 (may be NULL)
 * receives the original's result register where the routine returns one
 * (for the two tail calls, the callee's v0). */

/* em_area00_fx_gs.c */
int em_area00_fx_001CD940(EmArea00Fx *s, uint64_t mode, uint64_t p0, uint64_t c0, uint64_t p1, uint64_t c1);
int em_area00_fx_001D6DD0(EmArea00Fx *s, uint64_t ch, uint64_t a1, uint64_t a2, uint64_t *v0);
int em_area00_fx_001D7510(EmArea00Fx *s, uint64_t ch, uint64_t a1, uint64_t a2);
int em_area00_fx_001D7A80(EmArea00Fx *s, uint64_t ch, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                          uint64_t *v0);
int em_area00_fx_001F4A00(EmArea00Fx *s, uint64_t a0, uint64_t a1, uint64_t *v0);
int em_area00_fx_001F4E20(EmArea00Fx *s, uint64_t a0, uint64_t a1, uint32_t f12, uint64_t *v0);

/* em_area00_fx_spawn.c */
int em_area00_fx_001AA840(EmArea00Fx *s);
int em_area00_fx_001EF510(EmArea00Fx *s, uint64_t a0, uint64_t a1);
int em_area00_fx_001EFF10(EmArea00Fx *s, uint64_t id, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t t0,
                          uint64_t t1, uint32_t f12, uint64_t *v0);
int em_area00_fx_001EFFD0(EmArea00Fx *s, uint64_t id, uint64_t a1, uint64_t a2, uint64_t a3, uint32_t f12,
                          uint64_t *v0);
int em_area00_fx_001F0060(EmArea00Fx *s, uint64_t id, uint64_t owner);
int em_area00_fx_001F00A0(EmArea00Fx *s, uint64_t id, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t *v0);
int em_area00_fx_001F02C0(EmArea00Fx *s, uint64_t p, uint64_t id, uint32_t f12);

/* em_area00_fx_trail.c */
int em_area00_fx_001F1550(EmArea00Fx *s, uint64_t node, uint64_t count, uint64_t *v0);
int em_area00_fx_001F15F0(EmArea00Fx *s, uint64_t node, uint64_t slot, uint64_t row);
int em_area00_fx_001F18C0(EmArea00Fx *s, uint64_t node);

/* em_area00_fx_debris.c */
int em_area00_fx_001F2BA0(EmArea00Fx *s, uint64_t node);
int em_area00_fx_001F2E90(EmArea00Fx *s, uint64_t node, uint64_t row, uint64_t *v0);
int em_area00_fx_001F2F90(EmArea00Fx *s, uint64_t frame, uint64_t piece, uint64_t row, uint64_t kind);
int em_area00_fx_001F3340(EmArea00Fx *s, uint64_t piece, uint64_t row, uint64_t kind);
int em_area00_fx_001F3620(EmArea00Fx *s, uint64_t piece, uint64_t kind);
int em_area00_fx_001F3E30(EmArea00Fx *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t mode);

/* em_area00_fx_exit.c */
int em_area00_fx_001F4F40(EmArea00Fx *s, uint64_t sub, uint64_t *v0);
int em_area00_fx_001F4F90(EmArea00Fx *s, uint64_t node, uint32_t f12);
int em_area00_fx_001F5040(EmArea00Fx *s, uint64_t node);
int em_area00_fx_001F5490(EmArea00Fx *s, uint64_t p, uint64_t *v0);
int em_area00_fx_001F5F60(EmArea00Fx *s, uint64_t pos, uint64_t rot, uint64_t a2, uint64_t entry);

/* em_area00_fx_glow.c */
int em_area00_fx_001F6FB0(EmArea00Fx *s, uint64_t node);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA00_FX_H */
