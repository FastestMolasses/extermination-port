/* em_area00_hud.h - AREA00 (the third level) side track, lane A00HUD:
 * translations of the boot functions that AREA00 runs and that neither the
 * first level nor AREA01 ran, in the census subsystems hud_objects and
 * weapon_equip (docs/AREA00_HUD.md). Prefix em_area00_hud_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: census subsystem names, decomp comments and
 * FINDINGS are claims; the lines below say what the code does.
 *   001DEDB0  the render context D_00275670 + 0x2490 for a0 == 9, else
 *             + 0x2470 (asm words)
 *   001DEE80  001DEDB0(a0), then the three words at a1 to its +0x10..+0x18
 *             (asm)
 *   001DEEC0  001DEDB0(a0), then its word +4 = a1 (asm)
 *   001DF020  two 0x20-byte stack templates, the context's slot word, the
 *             seven slot-reset calls (byte-matched C)
 *   001DF110  001DF020(3, a0), one 0x10-byte header at the context's cursor
 *             +0x1C, 001CB760 (byte-matched C)
 *   001DF180  a 16 x 16 jittered grid on the stack, then 15 rows of
 *             0x430-byte packets at the context's slot cursor (NEARMISS;
 *             followed from the original code)
 *   001DF5A0  001DF180(3), the same header and 001CB760 as 001DF110 (C
 *             linked from asm)
 *   001E2800  VU0 projection of two points, a depth fade of two colours,
 *             one 4-quadword packet on the chain D_007635C0 (NEARMISS)
 *   001E2BA0  a 32-step shaded line from a0 to a1 through 001E2800 with a
 *             VU0 clip test per step (NEARMISS)
 *   001E2E80  pool owner, states 0..3: a moving hazard with a lifetime, two
 *             collision probes and a damage table (byte-matched C)
 *   001EAB50, 001EB980, 001EBBB0, 001EBC30, 001ECB00, 001ECFB0, 001ED7A0,
 *   001EEBA0, 001EEEB0  effect-kind draw handlers (001EA240 calls them with
 *             the node + 0xD0 and the depth key; each reads the work block
 *             through D_00275C34): 001CFB50 / 001CFBE0 draws, some with a
 *             001CD520 sprite, an LCG and an eased +8 (NEARMISS except
 *             001EAB50 / 001EB980, byte-matched)
 * 001E8E80 and 001E9280 (census hud_objects, first run in the AREA00
 * arrival) are NOT translated here: em_area01_exitb.c translates both and
 * its oracle verifies them (docs/AREA01_EXITB.md); this lane reuses them.
 *
 * Memory model. The routines address original EE memory through
 * caller-supplied regions keyed by original address: RAM, the scratchpad
 * and a stack region. Stack locals whose address the original hands to a
 * callee live in the original frame layout below the entry `sp` (001DF020's
 * two templates, 001DF180's grid and its two ST quadwords, 001E2BA0's five
 * vectors); every other local is a C local. Quadword accesses use the
 * address with its low four bits cleared, as the hardware does.
 *
 * VU0. The routines that use VU0 macro instructions (001E2800, 001E2BA0)
 * work on the VU0 state in the context (`vu`: vf1..vf31, ACC, Q and the
 * CLIP flag register; vf0 is the constant (0, 0, 0, 1)), writing exactly
 * the registers the original writes. 001E2800 reads vf23 and vf28..vf31 as
 * its caller left them (001E2BA0 loads them). One exception: the original
 * moves the whole 128-bit t0 register into vf3 after writing only its low
 * 64 bits (1.0 in x, 0 in y); vf3.z / w receive the caller's upper t0 half,
 * which the port does not model, so the module writes vf3.x / y only
 * (nothing reads vf3.z / w). The clip test (vclipw) is evaluated on DAZ'd
 * finite operands, as in em_render_context.c; a compared lane with exponent
 * 255, or a VU form outside the measured model, faults UNMEASURED.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address, the argument registers the original
 * sets for it (a0..t3 as 64-bit register images, f12..f19 as raw bits;
 * `na` / `nf` say how many), the stack pointer at the call and the module's
 * VU0 state (a callee that uses VU0 reads and writes it there). The worker
 * writes v0 / f0. Callees translated here (001DEDB0, 001DF020, 001DF180,
 * 001E2800) are called directly.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result, a NULL region list or an unmeasured float form latches the
 * fault (only the first one is kept); the call returns -1, and while `fault`
 * holds any value other than NONE every call returns -1 before doing any
 * work, until em_area00_hud_clear_fault. A NULL context returns -1 and
 * latches nothing. Bytes written before the faulting access stay written.
 * Output pointers are optional (NULL = not wanted).
 *
 * Oracle: tools/test_area00_hud_reference.py executes the original
 * instructions over the captured AREA00 RAM and compares, at every worker
 * call, the callee entry (stack pointer, argument registers, the bytes
 * behind stack pointer arguments, the VU0 state) and all of EE RAM and the
 * scratchpad, then again after the last store together with the result.
 * Its build defines EM_AREA00_HUD_STORE_TRACE (em_area00_hud.c) to learn
 * which lines the module stored to; ordinary builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA00_HUD_H
#define EM_AREA00_HUD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA00_HUD_FAULT_NONE       0
#define EM_AREA00_HUD_FAULT_NULL       1 /* NULL worker or region list */
#define EM_AREA00_HUD_FAULT_WORKER     2 /* the worker returned < 0 */
#define EM_AREA00_HUD_FAULT_UNMAPPED   3 /* an address outside every region */
#define EM_AREA00_HUD_FAULT_UNMEASURED 4 /* a float form or clip lane outside the measured model */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea00HudRegion;

/* The VU0 macro state (raw binary32 bits; lane 0 = x .. 3 = w). vf[0] is
 * never read or written (the module uses the constant (0, 0, 0, 1)). */
typedef struct {
    uint32_t vf[32][4];
    uint32_t acc[4];
    uint32_t q;
    uint32_t clip;  /* the 24-bit CLIP flag register (vi18) */
} EmArea00HudVu;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;       /* original callee address */
    uint32_t sp;       /* original stack pointer at the call */
    uint64_t a[8];     /* a0..a3, t0..t3 register images */
    uint32_t f[8];     /* f12..f19 raw bits */
    uint32_t na;       /* how many of a0..t3 are set (the callee's inputs) */
    uint32_t nf;       /* how many of f12..f19 are set */
    EmArea00HudVu *vu; /* the module's VU0 state (in / out) */
    uint64_t v0;       /* out: the callee's v0 */
    uint32_t f0;       /* out: the callee's f0 bits */
} EmArea00HudCall;

typedef int (*EmArea00HudWorker)(void *ctx, EmArea00HudCall *call);

typedef struct {
    const EmArea00HudRegion *regions;
    unsigned region_count;
    EmArea00HudWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA00_HUD_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL regions / UNMEASURED: 0 */
    EmArea00HudVu vu;        /* VU0 state (in / out) */
} EmArea00Hud;

void em_area00_hud_clear_fault(EmArea00Hud *s);

/* Every routine returns 0, or -1 on a fault. Pointer arguments are original
 * addresses; `f12` arguments are raw binary32 bits. `out`, where given,
 * receives the routine's 32-bit result (001DEDB0 / 001DF020 / 001DF180), or
 * for 001DEE80 / 001DEEC0 the record address they leave in v0. */
int em_area00_hud_001DEDB0(EmArea00Hud *s, uint32_t a0, uint32_t *out);
int em_area00_hud_001DEE80(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out);
int em_area00_hud_001DEEC0(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out);
int em_area00_hud_001DF020(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t *out);
int em_area00_hud_001DF110(EmArea00Hud *s, uint32_t a0);
int em_area00_hud_001DF180(EmArea00Hud *s, uint32_t a0, uint32_t f12, uint32_t *out);
int em_area00_hud_001DF5A0(EmArea00Hud *s, uint32_t f12);
int em_area00_hud_001E2800(EmArea00Hud *s, uint32_t mode, uint32_t pa, uint32_t ca, uint32_t pb, uint32_t cb);
int em_area00_hud_001E2BA0(EmArea00Hud *s, uint32_t a0, uint32_t a1, uint32_t a2);
int em_area00_hud_001E2E80(EmArea00Hud *s, uint32_t e);
int em_area00_hud_001EAB50(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001EB980(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001EBBB0(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001EBC30(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001ECB00(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001ECFB0(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001ED7A0(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001EEBA0(EmArea00Hud *s, uint32_t a0, uint32_t a1);
int em_area00_hud_001EEEB0(EmArea00Hud *s, uint32_t a0, uint32_t a1);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA00_HUD_H */
