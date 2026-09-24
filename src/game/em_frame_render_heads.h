/* em_frame_render_heads.h - the frame setup and projection heads (census lane
 * L32-frame-render-heads). Docs: docs/FRAME_RENDER_HEADS.md.
 *
 * Hand translation of these original functions (boot ELF SCUS-97112):
 *   001D1C50  per-frame render head: fog/weather selection, the context
 *             display-list REF tag, the projection, the SPR copies, the
 *             point-light tick and the per-slot constant fill (NEARMISS C;
 *             the .s was followed)
 *   001D1EA0  frame close: optional world flush, then the 001CB800 kick
 *   001D1EF0  tear-down frame: 001D1C50, 001D2830(3, 1), 001D1EA0(0)
 *   001D19E0  render subsystem reset (area load)
 *   001D19D0  thunk to 001D9070
 *   001D9070  the 0x16 model's per-vertex fade weights (NEARMISS C; .s followed)
 *   001D2830  context registration dispatch: < 0x20 -> 001D2730,
 *             < 0x40 -> 001E0C80, else 0 (asm-word unit; decoded from its words)
 *   001D2960  the projection: P, V, K = V x P, the four alternate P x V
 *             variants, the guard-band constants and the four cull planes
 *   001D2D20  one perspective matrix
 *   001D25F0  zoom store (spad 0x70003B60 and context +0x2468)
 *   001D2590  zoom from a size and a field angle: a / tanf(b / 2)
 *   001D2610  scope zoom helper: 001D2590(224, angle(arg0)) then 0021B970
 *   001C1D00  area render-env init step (state byte, 001E2260 tag in 0x1500)
 *   001D30A0  per-slot fills of the 14 skin records and the 0x2513E0 copy
 *             (NEARMISS C; the .s was followed)
 *   001D8060  light-slot lookup by id (context +0x220, 32 x 0x80)
 *   001D80B0  light-slot release by id
 *   001D88B0  lighting-mode dispatch: 001D8C30 or 001D8130/8340/8690
 *   001D8C30  fixed-lighting matrix/colour fill by mode (NEARMISS C; .s
 *             followed, including the order of every load and store)
 * and the SDK leaves they reach, translated privately: copy_qw4 (00102958),
 * 00102948 (one quadword copy), 001026D0 (VU0 4x4 product) and 001029C0 (VU0
 * identity).
 *
 * Memory model. These routines address original memory by address: the
 * render context is whatever D_00275670 points at, the display list is
 * wherever the cursor word at context +0x10 points, the 001D8C30 operands are
 * caller addresses. The host therefore hands the module a set of views,
 * each an original address range backed by storage the host owns. Every
 * load and store resolves its original address through the views; an
 * address no view covers, or a store into a read-only view, latches
 * EM_FRH_FAULT_BAD_ADDRESS. Quadword loads and stores (lq/sq, lqc2/sqc2)
 * clear the low four address bits as the EE does. Word and halfword
 * accesses at a misaligned address fault (the EE would raise an address
 * error, which is not modelled). Multi-byte values are little-endian, as on
 * the EE.
 *
 * Arithmetic: every COP1 and VU0 macro instruction goes through
 * game/em_ee_float.h on raw bit patterns under its real form; a refused
 * form latches EM_FRH_FAULT_UNMEASURED_FORM. Float arguments cross this API
 * as raw uint32_t register bits.
 *
 * Fail-stop: an entry point first checks that every worker its call tree can
 * reach is bound and that every fixed address it touches is mapped (the
 * context, list and caller addresses are checked as soon as they are known,
 * before the first store the routine makes through them). A NULL worker, a
 * negative worker result or an unmapped address latches a fault; every later
 * call returns -1 until the caller clears h->fault.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_FRAME_RENDER_HEADS_H
#define EM_FRAME_RENDER_HEADS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Original data addresses the routines reach directly. */
#define EM_FRH_D_00275670 UINT32_C(0x00275670) /* word: render context address */
#define EM_FRH_D_00275674 UINT32_C(0x00275674) /* word: context slot block address */
#define EM_FRH_D_00275688 UINT32_C(0x00275688) /* word: 001D88B0 publishes D_00817BC0 */
#define EM_FRH_D_00817BC0 UINT32_C(0x00817BC0) /* only its address is used */
#define EM_FRH_D_00810610 UINT32_C(0x00810610) /* 64 bytes: the view matrix 001D1C50 projects */
#define EM_FRH_D_008106C4 UINT32_C(0x008106C4) /* byte: nonzero skips the fog selection */
#define EM_FRH_D_008106C6 UINT32_C(0x008106C6) /* byte: == 2 with 0015D2F0 == 2 */
#define EM_FRH_D_008106C7 UINT32_C(0x008106C7) /* byte: 001D1C50 0x80 path */
#define EM_FRH_D_00810700 UINT32_C(0x00810700) /* byte: area */
#define EM_FRH_D_00810701 UINT32_C(0x00810701) /* byte: sub-area */
#define EM_FRH_D_0028A56C UINT32_C(0x0028A56C) /* word: model bank for 001C6120 */
#define EM_FRH_D_007635C0 UINT32_C(0x007635C0) /* only its address is used (001CB800 a0) */
#define EM_FRH_D_00816440 UINT32_C(0x00816440) /* 14 skin records of 0x100 (2 slots of 0x80) */
#define EM_FRH_D_002513E0 UINT32_C(0x002513E0) /* 64 bytes: 001D30A0 copy of spad 0x3AC0 */
#define EM_FRH_SPR_3A40   UINT32_C(0x70003A40) /* 64 bytes: P copy */
#define EM_FRH_SPR_3AC0   UINT32_C(0x70003AC0) /* 64 bytes: K copy */
#define EM_FRH_SPR_3B60   UINT32_C(0x70003B60) /* word: zoom (001D25F0) */
#define EM_FRH_SPR_3B8D   UINT32_C(0x70003B8D) /* byte: 001D1C50 fog path selector */
#define EM_FRH_D_008101D0 UINT32_C(0x008101D0) /* byte: the 001C1D00 state both variants pass */

/* Render context offsets (from the D_00275670 value). */
#define EM_FRH_CTX_FLAGS      0x000Cu /* 001D2730's channel mask (not touched here) */
#define EM_FRH_CTX_CURSOR     0x0010u /* word: display-list cursor (channel 0) */
#define EM_FRH_CTX_SLOT       0x009Cu /* word: double-buffer slot index */
#define EM_FRH_CTX_FOG        0x00A0u /* 16 bytes copied by 001D30A0 */
#define EM_FRH_CTX_B0         0x00B0u /* 8 bytes: 001D1C50 copies to slot +0x360 */
#define EM_FRH_CTX_F8         0x00F8u /* float: 0021B970 first argument (001D2610) */
#define EM_FRH_CTX_FC         0x00FCu /* float: 0021B970 second argument base */
#define EM_FRH_CTX_1D8        0x01D8u /* word: 001D19E0 clears */
#define EM_FRH_CTX_1E8        0x01E8u /* word: 001D19E0 clears */
#define EM_FRH_CTX_LIGHTS     0x0220u /* 32 light slots of 0x80: +0xC id, +0x2C word */
#define EM_FRH_CTX_GUARD      0x2220u /* 32 bytes: guard-band constants */
#define EM_FRH_CTX_ALT        0x2240u /* 4 matrices: the alternate P x V products */
#define EM_FRH_CTX_P          0x2340u /* matrix: projection */
#define EM_FRH_CTX_V          0x2380u /* matrix: view */
#define EM_FRH_CTX_K          0x23C0u /* matrix: V x P */
#define EM_FRH_CTX_PLANES     0x2410u /* 4 x 16 bytes: cull planes */
#define EM_FRH_CTX_ZOOM       0x2468u /* float */
#define EM_FRH_CTX_LIGHT_MODE 0x246Cu /* word: 001D88B0 mode */

enum {
    EM_FRH_FAULT_NONE = 0,
    EM_FRH_FAULT_NULL_WORKER = 1,    /* a reached worker is NULL */
    EM_FRH_FAULT_WORKER_FAILED = 2,  /* a worker returned a negative value */
    EM_FRH_FAULT_BAD_ADDRESS = 4,    /* an original address no view covers / misaligned / read-only */
    EM_FRH_FAULT_UNMEASURED_FORM = 6 /* em_ee_float.h refused a form (unreachable today) */
};

typedef struct {
    uint32_t address; /* the original function executing */
    int32_t code;     /* EM_FRH_FAULT_* */
    uint32_t data;    /* the original data address or worker address involved */
} EmFrhFault;

/* One original address range backed by host storage. */
typedef struct {
    uint32_t address;
    uint32_t size;
    uint8_t *bytes;
    int writable;
} EmFrhView;

/* Workers: every original callee outside this lane, by address. Each
 * returns >= 0 on success; a negative result faults. Float register values
 * are raw bits. */
typedef struct {
    void *ctx;
    /* 001D2830's callees; a0/a1 are the registers it received. v0 -> *ret. */
    int (*w_001D2730)(void *ctx, int32_t a0, int32_t a1, int32_t *ret);
    int (*w_001E0C80)(void *ctx, int32_t a0, int32_t a1, int32_t *ret);
    /* 001B0070(): the area flag word (the request block word D_008106C8). */
    int (*w_001B0070)(void *ctx, uint32_t *flags);
    /* 0015D2F0(): the equipment variant. */
    int (*w_0015D2F0)(void *ctx, int32_t *ret);
    /* 0021B970(f12, f13), 0021B9A0(f12, f13, a0), 0021BA80(a0, a1, a2): fog. */
    int (*w_0021B970)(void *ctx, uint32_t f12, uint32_t f13);
    int (*w_0021B9A0)(void *ctx, uint32_t f12, uint32_t f13, int32_t a0);
    int (*w_0021BA80)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    /* 001D7C30(): the point-light tick. */
    int (*w_001D7C30)(void *ctx);
    /* 001D2910(a0): the render-option test; v0 -> *ret. */
    int (*w_001D2910)(void *ctx, int32_t a0, int32_t *ret);
    /* 001D1EA0's world flush pair. */
    int (*w_001E0D70)(void *ctx);
    int (*w_001DDA00)(void *ctx);
    /* 001CB800(a0, a1, a2, a3): the list kick; 001D1EA0 passes
     * (D_007635C0, 0, context, context + 4). */
    int (*w_001CB800)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
    /* 001C1D00's callees. 001E2260(a0): the 64-bit tag register. */
    int (*w_001E2260)(void *ctx, uint64_t tag);
    int (*w_001E0CF0)(void *ctx);
    int (*w_001D5370)(void *ctx);
    /* SDK: 0011E748 sqrtf and 0011E398 tanf (f12 -> f0). */
    int (*w_0011E748)(void *ctx, uint32_t x, uint32_t *result);
    int (*w_0011E398)(void *ctx, uint32_t x, uint32_t *result);
    /* 001D19E0's callees, in call order. */
    int (*w_skin_arena_init)(void *ctx);                 /* 001D2E20 */
    int (*w_001D9720)(void *ctx);
    int (*w_001DD940)(void *ctx);
    int (*w_001E0C30)(void *ctx);
    int (*w_001D9060)(void *ctx);
    int (*w_001D71F0)(void *ctx);
    int (*w_001D7BB0)(void *ctx);
    int (*w_001D2DE0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001E0CC0)(void *ctx);
    int (*w_001E0380)(void *ctx);
    /* 001D88B0's default path (the dynamic lighting build). */
    int (*w_001D8130)(void *ctx, int32_t a0, uint32_t a1);
    int (*w_001D8340)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, uint32_t t0);
    int (*w_001D8690)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3);
    /* 001C6120(bank, id): the model table lookup; v0 -> *handle. */
    int (*w_001C6120)(void *ctx, uint32_t bank, uint32_t id, uint32_t *handle);
} EmFrhWorkers;

typedef struct {
    const EmFrhView *views;
    uint32_t view_count;
    EmFrhWorkers workers;
    EmFrhFault fault; /* latched; cleared only by the caller */
    uint32_t fn;      /* internal: the original function executing */
} EmFrh;

/* Every entry returns 0, or -1 when a fault is (or already was) latched. */
int em_frh_001D1C50(EmFrh *h);
int em_frh_001D1EA0(EmFrh *h, int32_t a0);
int em_frh_001D1EF0(EmFrh *h);
int em_frh_001D19E0(EmFrh *h);
int em_frh_001D19D0(EmFrh *h);
int em_frh_001D9070(EmFrh *h);
int em_frh_001D2830(EmFrh *h, int32_t a0, int32_t a1, int32_t *ret);
/* 001D2960(a0): a0 = the address of the view matrix (001D1C50 passes D_00810610). */
int em_frh_001D2960(EmFrh *h, uint32_t view_address);
/* 001D2D20(m, f12 focal, f13 width, f14 height, f15 near, f16 far) into a
 * native matrix (its callers pass a stack local). */
int em_frh_001D2D20(EmFrh *h, uint32_t m[16], uint32_t focal, uint32_t width, uint32_t height,
                    uint32_t znear, uint32_t zfar);
int em_frh_001D25F0(EmFrh *h, uint32_t zoom);
int em_frh_001D2590(EmFrh *h, uint32_t size, uint32_t angle);
int em_frh_001D2610(EmFrh *h, uint32_t amount);
/* 001C1D00(a0): a0 = the address of its state byte (D_008101D0 in both variants). */
int em_frh_001C1D00(EmFrh *h, uint32_t state_address);
int em_frh_001D30A0(EmFrh *h);
/* 001D8060(id): *slot = the matching light slot's address, or 0. */
int em_frh_001D8060(EmFrh *h, int32_t id, uint32_t *slot);
int em_frh_001D80B0(EmFrh *h, int32_t id);
int em_frh_001D88B0(EmFrh *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
/* 001D8C30(mode, m, out, in): addresses of the 16-word matrix, the 16-word
 * output block and the 4-word input vector. */
int em_frh_001D8C30(EmFrh *h, int32_t mode, uint32_t m, uint32_t out, uint32_t in);

#ifdef __cplusplus
}
#endif

#endif /* EM_FRAME_RENDER_HEADS_H */
