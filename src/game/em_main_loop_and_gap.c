/* em_main_loop_and_gap.c - the original main loop 0x1AAE40, its vblank
 * handler 0x1AB140 and the gap routine 001050E8 (see em_main_loop_and_gap.h,
 * docs/MAIN_LOOP_AND_GAP.md).
 *
 * Read from gs_readback_queue_run.c (readable C of the whole symbol, code
 * byte-identical by the decomp's hand check) and from the split listing,
 * whose delay slots decide where each store sits between two calls; every
 * such store cites the call site it rides with. 001050E8 is read from its
 * .word listing (the decomp has no C for it). */
#include "game/em_main_loop_and_gap.h"

#include <stddef.h>

#define CALL(expr) do { if ((expr) < 0) return -1; } while (0)

/* The original reads its halfwords with a sign-extending load (lh). */
static int32_t sx16(int16_t v) { return (int32_t)v; }

/* ---- fail-stop checks --------------------------------------------------- */

static int init_bound(const EmMlgWorkers *w)
{
    return w && w->w001AB1E0 && w->w001FEE60 && w->w001AB370 && w->w001CCCC0 &&
           w->w001CCBD0 && w->w001AB430 && w->w001FB210 && w->w001F9820 &&
           w->w001F9780 && w->w00101548 && w->w001FF1E0 && w->w001D0F20 &&
           w->w001CCB10 && w->w001B5790 && w->w00225CC0 && w->w001AB650 &&
           w->w001AB740 && w->w001AED80 && w->io_store && w->spin;
}

static int frame_bound(const EmMlgWorkers *w)
{
    return w && w->w001D1AE0 && w->w001B57E0 && w->w001AEBE0 && w->w001AB6A0 &&
           w->w001FCA10 && w->w001AEE70 && w->w001FB100 && w->w001B5B70 &&
           w->w00100A60 && w->w0011B910 && w->w0011B5E0 && w->w0011B328 &&
           w->w0011AE88 && w->w0011A9D8 && w->w001D7410 && w->w001AB590 &&
           w->w00203350 && w->w001D1C10 && w->w001AB4E0 && w->w001015A8 &&
           w->w00101810 && w->w0010BAA0 && w->w00100550 && w->w001D2300 &&
           w->w001D2580 && w->io_store && w->spin;
}

static int handler_bound(const EmMlgWorkers *w)
{
    return w && w->cop0_di && w->gs_csr_load && w->w0010C710 && w->cop0_ei;
}

/* Every global a routine reads or writes must be bound to its storage. */
static int init_state_bound(const EmMlgState *s)
{
    return s && s->d810E88;
}

static int frame_state_bound(const EmMlgState *s)
{
    return s && s->d810E98 && s->d810E80 && s->d810E88 && s->d821058 &&
           s->spad3B70 && s->spad3B72 && s->spad3B94 && s->spad3B96 && s->spad3B64;
}

static int handler_state_bound(const EmMlgState *s)
{
    return s && s->d810E98 && s->d810E90 && s->d810E88 && s->d282184;
}

/* ---- 0x1AAE40..0x1AAF24: start-up ---------------------------------------- */

int em_mlg_001AAE40_init(const EmMlgWorkers *w, EmMlgState *s)
{
    if (!init_state_bound(s) || !init_bound(w)) return -1;
    void *c = w->context;
    int32_t result = 0;

    /* 0x1AAE48: a nonzero result ends in the self-branch at 0x1AAE58. */
    CALL(w->w001AB1E0(c, &result));
    if (result != 0) return EM_MLG_HANG;
    CALL(w->w001FEE60(c));
    /* 0x1AAE74 (the delay slot of the call at 0x1AAE70): timer-0 mode 0x83. */
    CALL(w->io_store(c, EM_MLG_T0_MODE, 0x83));
    CALL(w->w001AB370(c));
    CALL(w->w001CCCC0(c));
    CALL(w->w001CCBD0(c, 0, 0x3FFF, 0));
    CALL(w->w001AB430(c));
    CALL(w->w001FB210(c));
    CALL(w->w001F9820(c));
    CALL(w->w001F9780(c));
    CALL(w->w00101548(c, EM_MLG_HANDLER));     /* 0x1AAEB4; its result is unused */
    CALL(w->w001FF1E0(c, 0));
    CALL(w->w001D0F20(c));
    CALL(w->w001CCB10(c));
    CALL(w->w001B5790(c));
    CALL(w->w00225CC0(c));
    CALL(w->w001AB650(c));
    CALL(w->w001AB740(c, 0, EM_MLG_BOOT_TASK));
    /* 0x1AAEFC: wait until the handler has latched an odd field. */
    while (*s->d810E88 == 0)
        CALL(w->spin(c, EM_MLG_SPIN_FIELD));
    /* 0x1AAF24 (the delay slot of the call at 0x1AAF20): timer-0 count 0. */
    CALL(w->io_store(c, EM_MLG_T0_COUNT, 0));
    CALL(w->w001AED80(c, 0));
    return 0;
}

/* ---- 0x1AAF28..0x1AB134: one frame ---------------------------------------- */

int em_mlg_001AAE40_frame(const EmMlgWorkers *w, EmMlgState *s)
{
    if (!frame_state_bound(s) || !frame_bound(w)) return -1;
    void *c = w->context;
    int32_t result = 0;

    *s->d810E98 = 0;                                  /* A: 0x1AAF2C */
    CALL(w->w001D1AE0(c, sx16(*s->d810E80)));         /* B: 0x1AAF34 */
    CALL(w->w001B57E0(c));                            /* C: 0x1AAF3C */
    CALL(w->w001AEBE0(c));                            /* D: 0x1AAF44 */
    CALL(w->w001AB6A0(c));                            /* E: 0x1AAF4C */
    CALL(w->w001FCA10(c));                            /* F: 0x1AAF54 */
    CALL(w->w001AEE70(c));                            /* G: 0x1AAF5C */
    CALL(w->w001FB100(c));                            /* H: 0x1AAF64 */
    CALL(w->w001B5B70(c));                            /* I: 0x1AAF6C */
    CALL(w->w00100A60(c, 0, 0, &result));             /* J: 0x1AAF78 */
    if (result != 0) {                                /* 0x1AAF80: branch on the 32-bit v0 */
        CALL(w->w0011B910(c));
        CALL(w->w0011B5E0(c));
        CALL(w->w0011B328(c));
        CALL(w->w0011AE88(c));
        CALL(w->w0011A9D8(c));
    }
    CALL(w->w001D7410(c));                            /* K: 0x1AAFB0 */
    CALL(w->w001AB590(c));                            /* L: 0x1AAFB8 */
    if (*s->d821058 == 1) {                           /* 0x1AAFCC: unsigned byte == 1 */
        CALL(w->w00203350(c));                        /* M: 0x1AAFD4 */
        CALL(w->w001D1C10(c, sx16(*s->d810E80)));     /* N: 0x1AAFE0 */
        CALL(w->w001AEE70(c));                        /* O: 0x1AAFE8 */
    }
    while (*s->d810E98 == 0)                          /* P: 0x1AAFF0 */
        CALL(w->spin(c, EM_MLG_SPIN_VBLANK));
    CALL(w->io_store(c, EM_MLG_T0_COUNT, 0));         /* 0x1AB010 */
    CALL(w->w001AB4E0(c, sx16(*s->spad3B94), sx16(*s->spad3B96)));   /* R: 0x1AB020 */

    /* S: the two environments of the current buffer, each with the
     * complement of the field bit truncated to a halfword and sign
     * extended (the original's 48-bit shift pair). Both halfwords are
     * read again for each call. */
    uint32_t env = *s->d810E80 != 0 ? EM_MLG_ENV_811070 : EM_MLG_ENV_810F00;
    CALL(w->w001015A8(c, env, sx16(*s->spad3B70), sx16(*s->spad3B72),
                      sx16((int16_t)(1 - sx16(*s->d810E88)))));    /* 0x1AB070 */
    env = *s->d810E80 != 0 ? EM_MLG_ENV_8110F0 : EM_MLG_ENV_810F80;
    CALL(w->w00101810(c, env, sx16(*s->spad3B70), sx16(*s->spad3B72),
                      sx16((int16_t)(1 - sx16(*s->d810E88)))));    /* 0x1AB0C0 */
    CALL(w->w0010BAA0(c, 0));                                     /* T: 0x1AB0C8 */
    CALL(w->w00100550(c, EM_MLG_DISP_810EA0 +
                         (uint32_t)(sx16(*s->d810E80) * 40)));     /* U: 0x1AB0EC */
    CALL(w->w001D2300(c));                                        /* V: 0x1AB0F4 */

    /* W: the field bit is read before the flipped index is stored (in the
     * delay slot of the call at 0x1AB118). */
    int32_t field = sx16(*s->d810E88);
    *s->d810E80 = (int16_t)(1 - sx16(*s->d810E80));
    CALL(w->w001D2580(c, field));
    *s->spad3B64 += 1;                                /* 0x1AB134, the back-branch delay slot */
    return 0;
}

/* ---- 0x1AB140: the vblank interrupt handler ------------------------------- */

int em_mlg_001AB140(const EmMlgWorkers *w, EmMlgState *s, int32_t *v0)
{
    if (!handler_state_bound(s) || !handler_bound(w)) return -1;
    void *c = w->context;
    uint32_t status = 0;
    uint64_t csr = 0;
    int32_t wake = 0;

    /* 0x1AB14C..0x1AB160: repeat the disable until Status.EIE reads 0. */
    do {
        CALL(w->cop0_di(c, &status));
    } while ((status & UINT32_C(0x10000)) != 0);
    *s->d810E98 = (int32_t)((uint32_t)*s->d810E98 + 1u);
    *s->d810E90 = (int32_t)((uint32_t)*s->d810E90 + 1u);
    CALL(w->gs_csr_load(c, &csr));
    *s->d810E88 = (int16_t)((csr >> 13) & 1u);        /* FIELD */
    CALL(w->w0010C710(c, *s->d282184, &wake));
    CALL(w->cop0_ei(c));
    /* 0x1AB1D4: nothing writes v0 after the 0010C710 call, so the handler
     * returns 0010C710's result to the kernel's INTC dispatch. */
    if (v0) *v0 = wake;
    return 0;
}

/* ---- 0x1050E8: signed halfwords saturated to bytes ------------------------ */

void em_mlg_001050E8(uint8_t *dst, const int16_t *src)
{
    /* 24 passes (0x1050E8); each clamps 16 lanes against the 0x00FF
     * halfword constant at 0x105130 (signed minimum), then against zero
     * (signed maximum), and packs the low bytes in lane order. */
    for (int pass = 0; pass < 0x18; pass++) {
        int16_t lane[16];
        for (int i = 0; i < 16; i++) lane[i] = src[i];
        for (int i = 0; i < 16; i++) {
            int32_t v = lane[i];
            if (v > 0xFF) v = 0xFF;
            if (v < 0) v = 0;
            dst[i] = (uint8_t)v;
        }
        src += 16;
        dst += 16;
    }
}
