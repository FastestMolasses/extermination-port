/* em_static_world.h - the static world's packet builders and the render
 * background channel (lane STATIC; docs/STATIC_WORLD.md). Names describe
 * what the instructions do; they are not claims about what the player sees.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112; the
 * decomp C where it is byte-matched, the splat .s where the C is a NEARMISS,
 * asm words or undecompiled):
 *
 *   static-object emission (001D5370's callees, channel 0)
 *   001D4FB0  tail call 001D4F30(0, obj)                        (byte-matched C)
 *   001D4F30  one 0x30 (REF) tag per run of at most 0x1F8 blocks of the
 *             object: address obj + 0x40 + 0x820 * blocks before, qwc
 *             0x82 * run; runs until the count at obj +0 is used up (.s)
 *   001D4B20  001D4960(obj) then 001D4B10(obj)                  (byte-matched C)
 *   001D4960  001D4750(0), 001D2090(0, D_00239C90), then the skin-record
 *             REF (0x30, qwc 8, D_00816440 + (ctx +0x9C << 7)) (asm words)
 *   001D4B10  tail call 001D4A90(0, obj)                        (byte-matched C)
 *   001D4A90  the same runs as 001D4F30 (its readable C is wrong: .s followed)
 *   001D4DA0  001D4750(0), 001D2090(0, D_00237180), 001D1F80(0, 1, 0), then
 *             the skin-record REF                              (NEARMISS; .s)
 *   001D4750  (vif_build_unpack_const) the 0x80-byte constant block at
 *             D_00817240, then two CNT packets on the channel: 9 qwords
 *             (FLUSH + UNPACK of 8 qwords: D_70003AC0 and D_00817240..7F)
 *             and 5 qwords (UNPACK of 4 qwords: D_00817280..BF)  (.s)
 *   001D2090  (vif_append_ref_tag) REF 1 qword to *D_00275674, context
 *             +0x50 + 4 chan = target, CALL target               (.s)
 *   00102958  (copy_qw4) four quadwords, all loaded before any store (.s)
 *
 *   the background channel (001E0CF0's callees)
 *   001E1E60  channel list start = the cursor; 001D1F80(chan, 0, 7),
 *             001D1FF0(chan, 0), 001D6F60(chan, ctx +0x1D0, 0x80), RGBAQ
 *             from the four floats at ctx +0x1C0 (x 128, 00128250), the
 *             matrix D_00253570 = transpose(ctx +0x2380), its second row
 *             doubled, times the X/Y swap (001026D0), D_002535B8 = zoom,
 *             four 001D7100 uploads, the REF to D_0023C990, MSCAL 0, the
 *             flag-0x23 sound branch, 001D2040(chan, 1) and a 0x60 (RET)
 *             tag; returns the start (NEARMISS C; .s followed: a0 is not
 *             read)
 *   001E1AD0  the 32 x 32 grid variant (flag 0x22; the first level never
 *             reaches it): the phase D_00275C0C, 001E1760, the TEX0 /
 *             state packets, 32 rows of 32 cells through 001E0E80, 001E17E0
 *             and the RET tag (NEARMISS C at 53%; .s followed)
 *   001E0E80  one grid point: view ray through transpose(ctx +0x2380)
 *             (001026A0), the ST pair and the clamped alpha      (.s)
 *   001D6F60  TEX0 + TEXA packet (6 qwords)                      (.s)
 *   001D7000  one-register A+D packet (4 qwords)                  (.s)
 *   001D7100  CNT + STCYCL 4,4 + UNPACK of n bytes to VU address a1,
 *             then block_copy of the data; returns the old cursor (.s)
 *   001D71A0  CNT tag + MSCAL a1                                  (.s)
 *   00121870  (block_copy) the C library forward copy              (.s)
 *   00102798  4x4 transpose over memory: all four rows are loaded
 *             before any store; the transpose is em_camera_commit_00102798
 *   001C6120  bank lookup: bank + (word[1 + (id & 0xFFFF & ~0x8000)] >> 2 << 2)
 *                                                                (byte-matched C)
 *
 * Reused, not translated again: 001026D0 is em_sdk_vu0_001026D0 (header),
 * the 00102798 transpose em_camera_commit_00102798;
 * every other callee is a worker named by its original address (the
 * binder hands the verified translations: em_load_veil_particles for
 * 001D1F80 / 001D1FF0 / 001D2040 / 001D7080 / 001D6BA0, em_render_context
 * for 001D2910 / 001D2E00, em_stream_lanes_00128250, em_player_float_to_int
 * (001281C0), em_sdk_math_original (0011DF78), em_effect_original (001026A0)).
 *
 * Memory: every original byte is reached through views by its ORIGINAL
 * address. Fixed addresses are checked at entry; packet bytes (whose address
 * comes from a cursor) immediately before each packet is written. A word,
 * halfword or doubleword access at an address the EE would reject (not
 * naturally aligned) faults like an address no view covers (the original
 * would take an address exception); quadword accesses ignore the low four
 * bits as the hardware does.
 *
 * Fail-stop: a reached NULL worker faults before the entry that reaches it
 * writes anything (each entry checks every worker it and its nested
 * translations can reach); a negative worker result faults at once; the
 * first fault is latched and every later call returns -1.
 *
 * Arithmetic: every COP1 operation goes through game/em_ee_float.h on raw
 * binary32 bits (docs/EE_FLOAT_MODEL.md); 001026D0 through em_sdk_vu0.h.
 *
 * stdint only, plus the header-only em_ee_float.h / em_sdk_vu0.h and
 * em_camera_commit_original (00102798). */
#ifndef EM_STATIC_WORLD_H
#define EM_STATIC_WORLD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original data addresses the routines use. */
#define EM_SW_D_00275670 0x00275670u /* the render context address word */
#define EM_SW_D_00275674 0x00275674u /* the GS block address word (001D2090's REF) */
#define EM_SW_D_0027569C 0x0027569Cu /* 001E1E60's sound handle (flag 0x23) */
#define EM_SW_D_00275C0C 0x00275C0Cu /* 001E1AD0's phase (float) */
#define EM_SW_D_0028A5A0 0x0028A5A0u /* the static-object bank address word */
#define EM_SW_D_00237180 0x00237180u /* 001D4DA0's CALL target */
#define EM_SW_D_00239C90 0x00239C90u /* 001D4960's CALL target */
#define EM_SW_D_0023C990 0x0023C990u /* 001E1E60's CALL target (the grid kernel) */
#define EM_SW_D_00253560 0x00253560u /* 001E1E60's 16-byte upload (GIF tag) */
#define EM_SW_D_00253570 0x00253570u /* 001E1E60's matrix + constants (0x80 upload) */
#define EM_SW_D_002535B8 0x002535B8u /* 001E1E60's zoom copy */
#define EM_SW_D_00816440 0x00816440u /* the skin records (0x80 each) */
#define EM_SW_D_00817240 0x00817240u /* 001D4750's 0x80-byte constant block */
#define EM_SW_D_008105D4 0x008105D4u /* 001E0E80's distance word (float) */
#define EM_SW_D_70003AC0 0x70003AC0u /* 001D4750's first upload (scratchpad) */

/* Packet bytes the builders write (fixed by the original code). */
#define EM_SW_001D4750_BYTES 0x100u /* 0xA0 + 0x60 */
#define EM_SW_001D2090_BYTES 0x20u
#define EM_SW_SKIN_TAG_BYTES 0x10u
#define EM_SW_001D6F60_BYTES 0x60u
#define EM_SW_001D7000_BYTES 0x40u
#define EM_SW_001D71A0_BYTES 0x20u
#define EM_SW_001E1AD0_ROW_BYTES 0xC30u

enum {
    EM_SW_FAULT_NONE = 0,
    EM_SW_FAULT_NULL_WORKER = 1,   /* a reachable worker is NULL */
    EM_SW_FAULT_WORKER_FAILED = 2, /* a worker returned a negative value */
    EM_SW_FAULT_BAD_ADDRESS = 4,   /* no view covers the address, or it is misaligned */
    EM_SW_FAULT_UNMEASURED = 6,    /* em_ee_float.h refused a VU0 form */
    EM_SW_FAULT_READ_ONLY = 8      /* a store into a view marked read-only */
};

typedef struct {
    uint32_t address; /* the original function executing */
    int32_t code;     /* EM_SW_FAULT_* */
    uint32_t detail;  /* the data or worker address involved */
} EmStaticWorldFault;

/* Host bytes standing for original addresses [address, address + size). */
typedef struct {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
} EmStaticWorldView;

/* Workers: the originals this module does not translate. Each returns >= 0
 * on success; a negative result faults. Integer arguments are the original
 * register values; float arguments and results are raw binary32 bits. */
typedef struct {
    void *ctx;
    /* REF tag builders and the RGBAQ packet (em_load_veil_particles). */
    int (*w_001D1F80)(void *ctx, int32_t chan, int32_t a1, int32_t a2);
    int (*w_001D1FF0)(void *ctx, int32_t chan, int32_t a1);
    int (*w_001D2040)(void *ctx, int32_t chan, int32_t a1);
    int (*w_001D7080)(void *ctx, int32_t chan, uint32_t rgba, uint32_t f12);
    /* 00128250(f12): the soft-float float -> unsigned. */
    int (*w_00128250)(void *ctx, uint32_t f12, uint32_t *result);
    /* 001D2910(flag): the render flag test; *result = its v0. */
    int (*w_001D2910)(void *ctx, int32_t flag, uint32_t *result);
    /* 001E1E60's flag-0x23 branch (the first level never sets flag 0x23).
     * snd / vol are the four words the original passes by stack address. */
    int (*w_00122BB8)(void *ctx, int32_t *result);                     /* rand */
    int (*w_001D73A0)(void *ctx, int32_t handle, int32_t *result);
    int (*w_001D72D0)(void *ctx, const uint32_t snd[4], int32_t *result);
    int (*w_001D75E0)(void *ctx, int32_t chan, const uint32_t snd[4], uint64_t a2, uint32_t a3,
                      uint32_t f12, uint32_t f13);
    int (*w_001CEFD0)(void *ctx, const uint32_t snd[4], const uint32_t vol[4]);
    /* 001E1AD0's callees (flag 0x22; the first level never sets it). */
    int (*w_001D2E00)(void *ctx, int32_t a0, uint32_t *result);
    int (*w_001E1760)(void *ctx, int32_t chan);
    int (*w_001D6BA0)(void *ctx, int32_t chan, int32_t a1, int32_t a2, int32_t a3, int32_t t0,
                      int32_t t1);
    int (*w_001281C0)(void *ctx, uint32_t f12, int32_t *result); /* float_to_int */
    int (*w_001E17E0)(void *ctx, int32_t chan);
    /* 001E0E80's: 001026A0(out, m, v) over the stack copies; 0011DF78 fabsf. */
    int (*w_001026A0)(void *ctx, uint32_t out[4], const uint32_t m[16], const uint32_t v[4]);
    int (*w_0011DF78)(void *ctx, uint32_t x, uint32_t *result);
} EmStaticWorldWorkers;

/* Test hook: called at the entry of every routine this module translates
 * and just before every worker call, with the original address and the
 * argument registers: the integer ones in order (a0..a3, t0, t1), then the
 * float ones (f12, f13) as raw bits; unused slots are 0. A stack address
 * the original passes is given as EM_SW_TRACE_STACK. NULL in the game. */
#define EM_SW_TRACE_STACK 0xFFFFFFFFu
typedef void (*EmStaticWorldTrace)(void *ctx, uint32_t address, const uint64_t args[6]);

typedef struct {
    const EmStaticWorldView *views; /* searched in order */
    uint32_t view_count;
    EmStaticWorldWorkers workers;
    EmStaticWorldFault fault; /* latched; cleared only by the caller */
    EmStaticWorldTrace trace;
    void *trace_ctx;
    uint32_t fn; /* internal: the original function executing */
    /* Optional, one flag per view (NULL: every view writable). A store into
     * a view whose flag is nonzero faults (EM_SW_FAULT_READ_ONLY). */
    const uint8_t *read_only;
} EmStaticWorld;

/* ---- Entries. 0 on success, -1 on a fault. `obj`, `target`, `src` and
 * the like are original addresses; chan is the channel index the original
 * receives (any value; the cursor is context +0x10 + 4 * chan). Where the
 * original returns a value, *result receives it (result may be NULL). ---- */
int em_static_world_001D4FB0(EmStaticWorld *s, uint32_t obj);
int em_static_world_001D4F30(EmStaticWorld *s, int32_t chan, uint32_t obj);
int em_static_world_001D4B20(EmStaticWorld *s, uint32_t obj);
/* 001D4960(a0): a0 is not read. */
int em_static_world_001D4960(EmStaticWorld *s, uint32_t a0);
int em_static_world_001D4B10(EmStaticWorld *s, uint32_t obj);
int em_static_world_001D4A90(EmStaticWorld *s, int32_t chan, uint32_t obj);
int em_static_world_001D4DA0(EmStaticWorld *s);
int em_static_world_001D4750(EmStaticWorld *s, int32_t chan);
int em_static_world_001D2090(EmStaticWorld *s, int32_t chan, uint32_t target);
/* 001E1E60(a0, chan): a0 is not read (001E0CF0 passes ctx + 0x180). */
int em_static_world_001E1E60(EmStaticWorld *s, uint32_t a0, int32_t chan, uint32_t *result);
/* 001E1AD0(a0, chan): a0 is not read (001E0CF0 passes ctx + 0x1E0). */
int em_static_world_001E1AD0(EmStaticWorld *s, uint32_t a0, int32_t chan, uint32_t *result);
/* 001E0E80(out, x, y): out is an original address (three words). */
int em_static_world_001E0E80(EmStaticWorld *s, uint32_t out, int32_t x, int32_t y, uint32_t *result);
int em_static_world_001D6F60(EmStaticWorld *s, int32_t chan, uint64_t tex0, int32_t a2);
int em_static_world_001D7000(EmStaticWorld *s, int32_t chan, int32_t a1);
int em_static_world_001D7100(EmStaticWorld *s, int32_t chan, uint32_t vu_address, uint32_t src,
                             int32_t bytes, uint32_t *result);
int em_static_world_001D71A0(EmStaticWorld *s, int32_t chan, uint32_t a1);
/* 001C6120(bank, id): *result = the entry address (no memory besides the
 * table word is read). */
int em_static_world_001C6120(EmStaticWorld *s, uint32_t bank, int32_t id, uint32_t *result);
/* 00102798(dst, src), 00102958 copy_qw4(dst, src), 00121870 block_copy(dst,
 * src, n): memory forms. 001026D0(dst, a, b): em_sdk_vu0_001026D0 over
 * memory (the form 001D5370's worker takes). */
int em_static_world_00102798(EmStaticWorld *s, uint32_t dst, uint32_t src);
int em_static_world_00102958(EmStaticWorld *s, uint32_t dst, uint32_t src);
int em_static_world_00121870(EmStaticWorld *s, uint32_t dst, uint32_t src, uint32_t n);
int em_static_world_001026D0(EmStaticWorld *s, uint32_t dst, uint32_t a, uint32_t b);

#ifdef __cplusplus
}
#endif

#endif /* EM_STATIC_WORLD_H */
