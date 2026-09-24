/* em_main_loop_and_gap.h - the original main loop 0x1AAE40 (its start-up
 * part, one frame of its endless loop, and the vblank interrupt handler
 * 0x1AB140 that shares its symbol), plus the unlisted libmpeg routine
 * 001050E8 in the 0x1050E4..0x105148 gap (docs/MAIN_LOOP_AND_GAP.md).
 *
 * Translations of the original instructions, read from the decomp's
 * readable C (gs_readback_queue_run.c, a structural NEARMISS whose code is
 * byte-identical by hand check) and the split listing (the loop's delay
 * slots, which fix the order of every store against every call, and the
 * .word listing of 001050E8):
 *
 *   0x1AAE40..0x1AAF24  start-up: 18 bring-up calls, the timer-0 mode
 *                       store, the wait for an odd field, the first
 *                       transition clear         -> em_mlg_001AAE40_init
 *   0x1AAF28..0x1AB134  one frame, steps A..W of docs/ORIGINAL_FRAME_ORDER.md
 *                                                -> em_mlg_001AAE40_frame
 *   0x1AB140..0x1AB1D8  the vblank handler (installed through 00101548)
 *                                                -> em_mlg_001AB140
 *   0x1050E8..0x105147  saturate 384 signed halfwords to bytes 0..255
 *                       (called only by 001041E8) -> em_mlg_001050E8
 *
 * Conventions (house style of em_player_stage_workers.h and
 * em_startup_load_gaps.h):
 *   - Every original callee is an explicit worker; so are the three
 *     hardware boundaries the loop touches directly (the timer-0 register
 *     stores, the GS CSR load in the handler, and the COP0 interrupt
 *     disable / enable), and one pass of each polling loop (the only place
 *     where the asynchronous vblank interrupt can be observed by the loop).
 *     A worker returns a negative value on a fault; results come back
 *     through out-parameters.
 *   - Each routine checks, before its first write, that every worker it can
 *     reach is bound, and returns -1 otherwise (fail-stop). A worker fault
 *     returns -1 at once; the writes made before it stay, in the original
 *     order.
 *   - Storage is named by original address; EE addresses passed to workers
 *     stay 32-bit EE addresses.
 *
 * Oracle: tools/test_main_loop_and_gap_reference.py executes the original
 * instructions of all four pieces over the captured route RAM and synthetic
 * states and compares every worker call (order and arguments), every
 * hardware access and every written field; it also compares the frame's
 * call sites with the PCSX2 frame traces of the original. */
#ifndef EM_MAIN_LOOP_AND_GAP_H
#define EM_MAIN_LOOP_AND_GAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* em_mlg_001AAE40_init result when 001AB1E0 reports a failure: the
 * original then spins forever on a branch to itself (0x1AAE58) and never
 * reaches the frame loop. The caller must not run a frame after it. */
#define EM_MLG_HANG 1

/* The globals the loop and its interrupt handler own or read, bound by
 * pointer to their ONE storage (never a private copy): other original code
 * on the live path reads and writes the same bytes, and its translations
 * must see the loop's and the handler's stores at once. Readers and writers
 * found by symbol name in the decomp's C and listings (references through
 * another symbol plus an offset are not covered):
 *   D_00810E90  read by 001F9CF0 (per-frame stream-lane service, reached
 *               from step H 001FB100) as an elapsed-vblank timer, and by
 *               001FA790 (lane start), which stores its value into the lane
 *               record; translated in em_stream_lanes_original (its
 *               globals view field d810E90).
 *   D_00810E88  read by 001D2300 (step V: 1 - field, as step S passes it).
 *   D_00810E80  written 0 by 001AB430 (start-up); read by 001AEBE0 (D),
 *               001AEE70 (G/O), 001CB800, 001CB8A0, 001CFBE0; the head
 *               sprite translation has a d810E80 view.
 *   D_00821058  written 1 by 001AC3B0, 001AD360, 001B7A30 (movie requests),
 *               0 by 001AB430 and 00203350 (the movie driver, step M);
 *               read by 001FB100 (H) and 001B7A30; the port has views in
 *               em_startup_load_gaps (EmSlgSoundFrame) and em_area_script.
 *   D_00282184  written by 001F9780 (start-up: the thread id it creates).
 *   0x70003B64  read by several game routines (e.g. 0015A2C0's every-128-
 *               frames test and 001C02E0) and cleared by 001AB430.
 *   0x70003B70/72/94/96  set by 001AB370 at start-up (0x800, 0x800, 0,
 *               0); 0x70003B70/72 are read by 001D2300; 0x70003B94/96 (the
 *               display offset) are adjusted by 00201F70 and 00201C50 and
 *               also referenced by 001AF150.
 * A NULL pointer that a routine needs makes it return -1 before any write
 * (fail-stop), like a missing worker. */
typedef struct EmMlgState {
    int32_t       *d810E98;   /* D_00810E98: vblank flag; A clears it, the handler increments it */
    int32_t       *d810E90;   /* D_00810E90: vblank count, incremented by the handler */
    int16_t       *d810E80;   /* D_00810E80: double-buffer index, flipped at step W */
    int16_t       *d810E88;   /* D_00810E88: GS CSR FIELD bit latched by the handler */
    const uint8_t *d821058;   /* D_00821058: 1 selects the movie block M/N/O (read only) */
    const int32_t *d282184;   /* D_00282184: the thread id the handler passes to 0010C710 (read only) */
    const int16_t *spad3B70;  /* 0x70003B70 / 0x70003B72: passed to 001015A8 and 00101810 (read only) */
    const int16_t *spad3B72;
    const int16_t *spad3B94;  /* 0x70003B94 / 0x70003B96: passed to 001AB4E0 (read only) */
    const int16_t *spad3B96;
    uint32_t      *spad3B64;  /* 0x70003B64: the main-loop counter, +1 at the end of every frame */
} EmMlgState;

/* Polling-loop sites passed to the spin worker. */
#define EM_MLG_SPIN_FIELD  UINT32_C(0x001AAEFC)  /* start-up: wait for D_00810E88 != 0 */
#define EM_MLG_SPIN_VBLANK UINT32_C(0x001AAFF0)  /* step P: wait for D_00810E98 != 0 */

/* The two timer-0 registers the loop stores to. */
#define EM_MLG_T0_COUNT UINT32_C(0x10000000)
#define EM_MLG_T0_MODE  UINT32_C(0x10000010)

/* EE addresses the loop passes. */
#define EM_MLG_HANDLER      UINT32_C(0x001AB140)  /* the vblank handler, to 00101548 */
#define EM_MLG_BOOT_TASK    UINT32_C(0x001AB7E0)  /* slot-0 task, to 001AB740 */
#define EM_MLG_ENV_810F00   UINT32_C(0x00810F00)  /* 001015A8 argument, buffer 0 */
#define EM_MLG_ENV_811070   UINT32_C(0x00811070)  /* 001015A8 argument, buffer != 0 */
#define EM_MLG_ENV_810F80   UINT32_C(0x00810F80)  /* 00101810 argument, buffer 0 */
#define EM_MLG_ENV_8110F0   UINT32_C(0x008110F0)  /* 00101810 argument, buffer != 0 */
#define EM_MLG_DISP_810EA0  UINT32_C(0x00810EA0)  /* 00100550 argument base, 40 bytes per buffer */

typedef struct EmMlgWorkers {
    void *context;

    /* ---- start-up (0x1AAE40..0x1AAF24), in call order ---- */
    int (*w001AB1E0)(void *context, int32_t *result);  /* IOP module bring-up; nonzero = failure */
    int (*w001FEE60)(void *context);
    int (*w001AB370)(void *context);
    int (*w001CCCC0)(void *context);
    int (*w001CCBD0)(void *context, int32_t a0, int32_t a1, int32_t a2);  /* (0, 0x3FFF, 0) */
    int (*w001AB430)(void *context);
    int (*w001FB210)(void *context);
    int (*w001F9820)(void *context);
    int (*w001F9780)(void *context);
    int (*w00101548)(void *context, uint32_t handler);  /* installs 0x1AB140; result unused */
    int (*w001FF1E0)(void *context, int32_t a0);        /* (0) */
    int (*w001D0F20)(void *context);
    int (*w001CCB10)(void *context);
    int (*w001B5790)(void *context);
    int (*w00225CC0)(void *context);
    int (*w001AB650)(void *context);                    /* task table reset */
    int (*w001AB740)(void *context, int32_t slot, uint32_t task);  /* (0, 0x1AB7E0) */
    int (*w001AED80)(void *context, int32_t colour);    /* (0): transition clear */

    /* ---- one frame (0x1AAF28..0x1AB134), in call order ---- */
    int (*w001D1AE0)(void *context, int32_t buffer);    /* B: D_00810E80 */
    int (*w001B57E0)(void *context);                    /* C: pad read */
    int (*w001AEBE0)(void *context);                    /* D: letterbox fade */
    int (*w001AB6A0)(void *context);                    /* E: task dispatch */
    int (*w001FCA10)(void *context);                    /* F: message service */
    int (*w001AEE70)(void *context);                    /* G and O: transition fade */
    int (*w001FB100)(void *context);                    /* H: sound bookkeeping */
    int (*w001B5B70)(void *context);                    /* I: actuator countdown */
    int (*w00100A60)(void *context, int32_t a0, int32_t a1, int32_t *result);  /* J: (0, 0) */
    int (*w0011B910)(void *context);                    /* J block, when 00100A60 != 0 */
    int (*w0011B5E0)(void *context);
    int (*w0011B328)(void *context);
    int (*w0011AE88)(void *context);
    int (*w0011A9D8)(void *context);
    int (*w001D7410)(void *context);                    /* K */
    int (*w001AB590)(void *context);                    /* L: DMA CHCR ASP watchdog */
    int (*w00203350)(void *context);                    /* M: the blocking movie driver */
    int (*w001D1C10)(void *context, int32_t buffer);    /* N: D_00810E80 */
    int (*w001AB4E0)(void *context, int32_t dx, int32_t dy);  /* R: 0x70003B94, 0x70003B96 */
    /* S: (env, 0x70003B70, 0x70003B72, (int16_t)(1 - D_00810E88)) */
    int (*w001015A8)(void *context, uint32_t env, int32_t a1, int32_t a2, int32_t a3);
    int (*w00101810)(void *context, uint32_t env, int32_t a1, int32_t a2, int32_t a3);
    int (*w0010BAA0)(void *context, int32_t a0);        /* T: (0), kernel syscall 100 stub */
    int (*w00100550)(void *context, uint32_t env);      /* U: 0x810EA0 + 40 * D_00810E80 */
    int (*w001D2300)(void *context);                    /* V */
    int (*w001D2580)(void *context, int32_t field);     /* W: D_00810E88 */

    /* ---- boundaries of the loop itself ---- */
    int (*io_store)(void *context, uint32_t address, uint32_t value);  /* 32-bit timer-0 store */
    /* One pass of a polling loop at `site`, after it read the flag as 0
     * and before it reads it again: the moment at which the asynchronous
     * vblank interrupt (em_mlg_001AB140) can be delivered. */
    int (*spin)(void *context, uint32_t site);

    /* ---- the vblank handler 0x1AB140 ---- */
    /* di, sync.p, then the COP0 Status register (the handler repeats this
     * while Status.EIE, bit 16, still reads as set). */
    int (*cop0_di)(void *context, uint32_t *status);
    int (*gs_csr_load)(void *context, uint64_t *csr);   /* 64-bit load of 0x12001000 */
    /* (D_00282184); *result = its v0, which the handler returns */
    int (*w0010C710)(void *context, int32_t thread, int32_t *result);
    int (*cop0_ei)(void *context);                      /* sync, ei */
} EmMlgWorkers;

/* 0x1AAE40..0x1AAF24: returns 0 at the loop top 0x1AAF28, EM_MLG_HANG when
 * 001AB1E0 fails, -1 on a fault. Writes no field of *s itself (the field
 * wait reads D_00810E88, which only the handler sets). */
int em_mlg_001AAE40_init(const EmMlgWorkers *w, EmMlgState *s);

/* 0x1AAF28..0x1AB134: one pass of the endless loop (steps A..W). Returns 0
 * when the pass reaches the back branch to 0x1AAF28, -1 on a fault. */
int em_mlg_001AAE40_frame(const EmMlgWorkers *w, EmMlgState *s);

/* 0x1AB140: the vblank interrupt handler. 0, or -1 on a fault. On success
 * *v0 (when v0 is not NULL) receives the handler's return value: the
 * original writes nothing to v0 after its 0010C710 call, so it returns
 * 0010C710's result, which the EE kernel's INTC dispatch consumes (00101548
 * installs the handler with AddIntcHandler(2, handler, -1)). The native
 * platform has no kernel handler chain; it may pass NULL (the value is then
 * dropped at that boundary, and nothing in the game reads it). On a fault
 * *v0 is left unchanged. */
int em_mlg_001AB140(const EmMlgWorkers *w, EmMlgState *s, int32_t *v0);

/* 0x1050E8: dst[i] = clamp(src[i], 0, 255) for 384 signed halfwords, 16 per
 * pass in 24 passes. Like the original, each pass reads its whole 32-byte
 * source block before it stores its 16 bytes, so overlapping buffers give
 * the original's result too. The original's two quadword loads and its
 * quadword store need 16-byte aligned addresses; the native routine has no
 * alignment requirement. */
void em_mlg_001050E8(uint8_t *dst, const int16_t *src);

#ifdef __cplusplus
}
#endif

#endif /* EM_MAIN_LOOP_AND_GAP_H */
