/* Render context lane (census L30-render-context): the per-frame four-sprite
 * pass 001DDE10 and its dispatchers, the render-context flag words, the
 * grid pass 001D5370 and their set-up / tear-down helpers. Names here are
 * descriptions of what the instructions do, not claims about what the
 * player sees. Docs: docs/RENDER_CONTEXT.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112; the
 * decomp C where it is byte-matched, the splat .s where it is not):
 *
 *   lane functions
 *   001DD7B0  GS block D_0081C050 set-up + clears the 0x24-byte block at
 *             context +0x24F0 (NEARMISS C; the .s was followed: its 001006D8
 *             call passes t0 = 0, t1 = 2, which the C drops)
 *   001DD940  tail jump to 001DD7B0 (byte-matched)
 *   001DD950  context +0x2450 = the quadword at a0, +0x2460 = 16777215 / f12,
 *             +0x2464 = f13 (byte-matched)
 *   001DDA00  per-frame tick: 001DEEE0 on +0x2470 and +0x2490, then the flag
 *             gated 001DDAA0 / 001DDB70 / 001DFF70 (byte-matched)
 *   001DDAA0  area-key dispatch to 001DE920 or 001DDE10 (byte-matched)
 *   001DDE10  the four-sprite pass: one value projected through the
 *             001026A0 matrix, the D_00275690 / D_00275694 eases, four eased
 *             value/width pairs at +0x24F0, four 0x80-byte packets on the
 *             channel 3 cursor, the 0x10-byte end tag and the 001CB760 call
 *             (byte-matched)
 *   001DEEE0  the 8-step ramp machine on a 0x20-byte record (byte-matched)
 *   001DEDE0  the two 001DEEE0 ramp records' set-up: states and flag numbers
 *             2 / 9, the D_0026E850 colour, the 0x60 limit (asm words, with
 *             001DEDF0, 001DEDB0, 001DEE80 and 001DEEC0 inline)
 *   001E0C30  clears context +0x170..+0x177, then tail-jumps to 001E1010
 *             (byte-matched)
 *   001E0C60  context +0x174 bit test for flags 0x20..0x3F (byte-matched)
 *   001E0C80  context +0x174 bit set/clear; returns the OLD bit in both
 *             directions (C linked from asm: the .s was followed; the C
 *             returns 0 on a clear, the instructions do not)
 *   001E0CC0  001D2DE0(0, 0), context +0x1D8 = +0x1E8 = 0 (byte-matched)
 *   001E0D70  pending chain kick of context word +0x2520 (byte-matched)
 *   001E0DF0  releases +0x1D8 / +0x1E8 / +0x2520 through 001D21B0 tags
 *             (byte-matched)
 *   001E1010  the 16 x 16 table of 0x18-byte records at D_0081E0F0
 *             (byte-matched)
 *   001D5370  the grid pass: 32 x 32 cells x 4 slots of the grid at context
 *             +0x140, a VU0 clip test of each bank object's eight box
 *             corners against the matrix at D_70003400 (built for 1024 x 448)
 *             and, when some corner is outside, the one at D_70003440 (4096 x
 *             4096), then the 001D4FB0 / 001D4B20 calls (NEARMISS C; the .s
 *             was followed)
 *   001D52E0  grid header copy from static-object bank entry 0 into context
 *             +0x140..+0x167 (byte-matched)
 *
 *   helpers the lane functions reach, translated here because they only
 *   read or write render-context words or packet bytes (census: boundary)
 *   001D2730  flag set / clear for flags 0..0x1F, with the flag-0 moves of
 *             the +0xA0 / +0xC0 / +0x100 blocks (NEARMISS C; the .s was
 *             followed)
 *   001D2910 / 001D2710  flag query: a0 < 0x20 tests context +0x0C bit a0,
 *             0x20 <= a0 < 0x40 is 001E0C60, anything else returns 0
 *   001D2E00 / 001D2DE0  context word +0x2520 + 4 * a0 read / write
 *   001D21B0  one 0x10-byte tag at the context +0x08 cursor (byte-matched)
 *   001D6B10  001D6930(a0..a3, t0 = D_0026E510) then 001D1F20(a0)
 *   001D6C90  one 0x60-byte GIF packet of five A+D register writes at the
 *             channel cursor (NEARMISS C; the .s was followed)
 *
 * Every other callee is an explicit worker named by its original address
 * (EmRenderContextWorkers). A reached NULL worker faults before the routine
 * that reaches it writes anything (each entry checks every worker its
 * routine and its nested translations can reach); a negative worker result
 * faults at once, leaving the bytes written before that call.
 *
 * Memory: every original byte these routines read or write is reached
 * through EmRenderContextWorld.views by its ORIGINAL address, exactly as the
 * instructions address it. The fixed addresses a routine uses (context
 * fields, globals, scratchpad matrices) are checked at its entry; addresses
 * that come from memory (packet cursors, bank objects, the grid) are checked
 * immediately before the packet or record they cover is read or written.
 * An address no view covers faults (EM_RC_FAULT_BAD_INDEX). A quadword load
 * or store (the original's 128-bit memory accesses, GPR and VU) uses the
 * address with its low four bits cleared, as the hardware does.
 *
 * Arithmetic: every COP1 operation goes through game/em_ee_float.h on raw
 * binary32 bits; the VU0 products in 001D5370 use em_vu_vec_bits (VMULAbc,
 * VMADDAbc, VMADDbc, all measured forms). The VU clip test (vclipw) is not
 * part of the measured float model: this module compares DAZ'd magnitude
 * bits (x > |w| with x positive sets the plus flag, |x| > |w| with x
 * negative the minus flag) and faults with EM_RC_FAULT_UNMEASURED when a
 * compared lane has exponent 255. docs/RENDER_CONTEXT.md section 4.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_RENDER_CONTEXT_H
#define EM_RENDER_CONTEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original addresses the routines pass to workers or use as fixed data. */
#define EM_RC_D_0026E510 0x0026E510u /* 001D6B10's t0 for 001D6930 */
#define EM_RC_D_0026E850 0x0026E850u /* 001DEDE0's three ramp colour words */
#define EM_RC_D_0027568C 0x0027568Cu /* word 001DD7B0 reads; 001DDE10 passes it on */
#define EM_RC_D_00275690 0x00275690u /* 001DDE10's first eased float */
#define EM_RC_D_00275694 0x00275694u /* 001DDE10's second eased float */
#define EM_RC_D_0028A5A0 0x0028A5A0u /* the bank address word 001C6120 indexes */
#define EM_RC_D_00810360 0x00810360u /* 001DDE10's quadword source (v == 0) */
#define EM_RC_D_008104E0 0x008104E0u /* the word 001DDE10 tests for 0xC/0xD/0x29 */
#define EM_RC_D_00810610 0x00810610u /* 001D5370's view matrix (001026D0 a2) */
#define EM_RC_D_008106C6 0x008106C6u /* 001DDA00's byte (== 2) */
#define EM_RC_D_00810700 0x00810700u /* area key high byte (+1 low byte, +2 room) */
#define EM_RC_D_007635C0 0x007635C0u /* 001CB760's chain table (a0) */
#define EM_RC_D_0081C050 0x0081C050u /* 001DD7B0's GS block (0x30 bytes it touches) */
#define EM_RC_D_0081C070 0x0081C070u /* 001DD7B0's 001006D8 env block */
#define EM_RC_D_0081E0F0 0x0081E0F0u /* 001E1010's table (16 rows x 0x180) */
#define EM_RC_D_70003400 0x70003400u /* scratchpad matrix, 001D2D20(.., 1024, 448, ..) x D_00810610 */
#define EM_RC_D_70003440 0x70003440u /* scratchpad matrix, 001D2D20(.., 4096, 4096, ..) x D_00810610 */
#define EM_RC_D_70003AC0 0x70003AC0u /* scratchpad matrix 001DDE10 hands to 001026A0 */

/* Context offsets of the blocks the lane uses (context = *D_00275670). */
#define EM_RC_CTX_FLAGS_LO 0x000Cu  /* flags 0..0x1F (001D2710) */
#define EM_RC_CTX_CHAN3 0x001Cu     /* channel 3 packet cursor (+0x10 + 4 * 3) */
#define EM_RC_CTX_FLAGS_HI 0x0174u  /* flags 0x20..0x3F (001E0C60 / 001E0C80) */
#define EM_RC_CTX_GRID 0x0140u      /* 001D52E0 / 001D5370 grid header */
#define EM_RC_CTX_DEPTH 0x2450u     /* 001DD950 / 001DDE10 quadword + 2 floats */
#define EM_RC_CTX_RAMP_A 0x2470u    /* 001DEEE0 record (flag at +8) */
#define EM_RC_CTX_RAMP_B 0x2490u    /* 001DEEE0 record */
#define EM_RC_CTX_BARS 0x24F0u      /* 001DDE10 eased pairs (+0x00, +0x10) and +0x20 word */
#define EM_RC_CTX_SLOTS 0x2520u     /* 001D2E00 / 001D2DE0 words */

/* Packet bytes the translated builders write. */
#define EM_RC_001DDE10_SPRITE_BYTES 0x80u
#define EM_RC_001DDE10_END_BYTES 0x10u
#define EM_RC_001D6C90_BYTES 0x60u
#define EM_RC_001D21B0_BYTES 0x10u

/* Fault codes 1, 2 and 4 are numerically the EM_SCENE_FAULT_* codes of
 * em_scene_state.h; 6 has no scene twin. */
enum {
    EM_RC_FAULT_NONE = 0,
    EM_RC_FAULT_NULL_WORKER = 1,   /* a reachable worker is NULL */
    EM_RC_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_RC_FAULT_BAD_INDEX = 4,     /* an original address no view covers */
    EM_RC_FAULT_UNMEASURED = 6     /* a VU clip compare or form outside the model */
};

typedef struct {
    uint32_t address; /* original function or data address */
    int32_t code;     /* EM_RC_FAULT_* */
} EmRenderContextFault;

/* Host bytes standing for original addresses [address, address + size). */
typedef struct {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
} EmRenderContextView;

typedef struct {
    uint32_t ctx;                     /* the value of D_00275670 */
    const EmRenderContextView *views; /* searched in order */
    uint32_t view_count;
} EmRenderContextWorld;

/* Workers, one per original callee that is not translated here. Each
 * returns >= 0 on success; a negative result faults. Integer arguments are
 * the original register values; float arguments and results are raw
 * binary32 bits. Addresses are original addresses. */
typedef struct {
    void *ctx;
    /* 0015D2F0(): its 0/1/2/3/0x82 code (001DDA00, 001DDE10). */
    int (*w_0015D2F0)(void *ctx, int32_t *result);
    /* 0022EBE0(): its 0/1 result (001DDE10's v). */
    int (*w_0022EBE0)(void *ctx, int32_t *result);
    /* 001B0070(): the area flag word D_008106C8 (001DDAA0, 001E0D70). */
    int (*w_001B0070)(void *ctx, uint32_t *result);
    /* 001026A0(out, m, v): out = m * v; 001DDE10 passes m = D_70003AC0 and
     * out == v (its stack quadword). */
    int (*w_001026A0)(void *ctx, uint32_t out[4], uint32_t matrix, const uint32_t v[4]);
    /* 0011DF78(x): fabsf. */
    int (*w_0011DF78)(void *ctx, uint32_t x, uint32_t *result);
    /* 001281C0(x): float_to_int. */
    int (*w_001281C0)(void *ctx, uint32_t x, int32_t *result);
    /* 001D6930(a0, a1, a2, a3, t0): frame-copy packet (001D6B10). */
    int (*w_001D6930)(void *ctx, int32_t a0, int32_t a1, int32_t a2, int32_t a3, uint32_t t0,
                      uint32_t *result);
    /* 001D1F20(chan): REF tag. */
    int (*w_001D1F20)(void *ctx, int32_t chan);
    /* 001D6BA0(chan, a1, a2, a3, t0, t1): TEX0 packet. */
    int (*w_001D6BA0)(void *ctx, int32_t chan, int32_t a1, int32_t a2, int32_t a3, int32_t t0,
                      int32_t t1);
    /* 001D1FF0(chan, a1): REF tag. */
    int (*w_001D1FF0)(void *ctx, int32_t chan, int32_t a1);
    /* 001D1F80(chan, a1, a2): REF tag (001D5370). */
    int (*w_001D1F80)(void *ctx, int32_t chan, int32_t a1, int32_t a2);
    /* 001006D8(env, psm, w, h, t0, t1): SDK draw-environment fill (001DD7B0). */
    int (*w_001006D8)(void *ctx, uint32_t env, int32_t psm, int32_t w, int32_t h, int32_t t0,
                      int32_t t1);
    /* 001CB760(table, page, handle): chain kick (a3 is not read). */
    int (*w_001CB760)(void *ctx, uint32_t table, int32_t page, uint32_t handle);
    /* 001D2D20(m, f12, f13, f14, f15, f16): projection matrix at m. */
    int (*w_001D2D20)(void *ctx, uint32_t m, uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15,
                      uint32_t f16);
    /* 001026D0(dst, a, b): 4x4 product into dst (001D5370: dst == a). */
    int (*w_001026D0)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    /* 001C6120(bank, id): *result = the bank entry address. */
    int (*w_001C6120)(void *ctx, uint32_t bank, int32_t id, uint32_t *result);
    /* The emission / effect callees (no result is read). */
    int (*w_001D4DA0)(void *ctx);
    int (*w_001D4FB0)(void *ctx, uint32_t obj);
    int (*w_001D4B20)(void *ctx, uint32_t obj);
    int (*w_001D5BD0)(void *ctx);
    int (*w_001DE920)(void *ctx);
    int (*w_001DDB70)(void *ctx);
    int (*w_001DFF70)(void *ctx);
    int (*w_001DF110)(void *ctx, uint32_t record); /* 001DEEE0: its record + 0x10 */
} EmRenderContextWorkers;

typedef struct EmRenderContext {
    EmRenderContextWorld world;
    EmRenderContextWorkers workers;
    EmRenderContextFault fault; /* latched; cleared only by the caller */
} EmRenderContext;

/* ---- The lane functions. 0 on success, -1 on a fault (a latched fault
 * makes every call return -1 without reading or writing). Where the
 * original returns a value, *result receives it (result may be NULL). ---- */
int em_render_context_001DD7B0(EmRenderContext *s);
int em_render_context_001DD940(EmRenderContext *s);
/* 001DD950(a0, f12, f13): a0 is an original address. */
int em_render_context_001DD950(EmRenderContext *s, uint32_t a0, uint32_t f12, uint32_t f13);
int em_render_context_001DDA00(EmRenderContext *s);
int em_render_context_001DDAA0(EmRenderContext *s);
int em_render_context_001DDE10(EmRenderContext *s);
/* 001DEEE0(p): p is the record's original address. */
int em_render_context_001DEEE0(EmRenderContext *s, uint32_t p);
int em_render_context_001E0C30(EmRenderContext *s);
int em_render_context_001E0C60(EmRenderContext *s, int32_t a0, uint32_t *result);
int em_render_context_001E0C80(EmRenderContext *s, int32_t a0, int32_t a1, uint32_t *result);
int em_render_context_001E0CC0(EmRenderContext *s);
int em_render_context_001E0D70(EmRenderContext *s);
int em_render_context_001E0DF0(EmRenderContext *s);
int em_render_context_001E1010(EmRenderContext *s);
/* 001DEDE0: the two ramp records' set-up (boot; 001DEDF0 and its 001DEDB0 /
 * 001DEE80 / 001DEEC0 inline). */
int em_render_context_001DEDE0(EmRenderContext *s);
int em_render_context_001D5370(EmRenderContext *s);
int em_render_context_001D52E0(EmRenderContext *s);

/* ---- The helpers (same conventions). ---- */
int em_render_context_001D2910(EmRenderContext *s, int32_t a0, uint32_t *result);
int em_render_context_001D2710(EmRenderContext *s, int32_t a0, uint32_t *result);
/* 001D2730(a0, a1): the flag set / clear for flags 0..0x1F at context +0x0C
 * (the channel-0 fog block moves included); *result = 1 when the bit was set
 * BEFORE. */
int em_render_context_001D2730(EmRenderContext *s, int32_t a0, int32_t a1, uint32_t *result);
int em_render_context_001D2E00(EmRenderContext *s, int32_t a0, uint32_t *result);
int em_render_context_001D2DE0(EmRenderContext *s, int32_t a0, uint32_t a1);
int em_render_context_001D21B0(EmRenderContext *s, uint32_t a0);
/* 001D6B10(chan, a1, a2, a3): *result = 001D6930's result. */
int em_render_context_001D6B10(EmRenderContext *s, int32_t a0, int32_t a1, int32_t a2, int32_t a3,
                               uint32_t *result);
/* 001D6C90(args[0..14]): a0..a3, t0..t3 and the seven stack words (the
 * original reads the low 32 bits of each). *result = packet + 0x10. */
int em_render_context_001D6C90(EmRenderContext *s, const int32_t args[15], uint32_t *result);

/* The VU clip test as implemented here (exposed for the test): flags of
 * x/y/z against |w| (bits +x, -x, +y, -y, +z, -z). 0, or -1 when a lane has
 * exponent 255 (not modelled). */
int em_render_context_clipw(const uint32_t v[4], uint32_t *flags);

#ifdef __cplusplus
}
#endif

#endif /* EM_RENDER_CONTEXT_H */
