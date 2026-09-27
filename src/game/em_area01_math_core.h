/* em_area01_math_core.h - the EE storage view and worker dispatch shared by
 * the AREA01 lane "math" translations (docs/AREA01_MATH.md).
 *
 * The AREA01 math / actor-anim / anim-runtime originals translated in
 * em_area01_math_actor.c, em_area01_math_owner.c and em_area01_math_player.c
 * work on original EE addresses: every pointer they receive or build is an
 * EE address, and every load and store goes through this view, which backs
 * EE RAM and the 16 KiB scratchpad (0x70000000) with host bytes. The
 * original's callees are reached through one dispatcher (`call`), keyed by
 * the original address of the callee, with the register arguments the
 * original passes (a0.. and f12.. as raw 32-bit words) and the v0 / f0
 * results. A host binds the dispatcher to port translations (or to a
 * fail-stop default); the reference test binds it to the original callee's
 * recorded boundary.
 *
 * Fail-stop. The first unmapped access, NULL view, missing dispatcher or
 * negative dispatcher result latches a fault (its EE address and code).
 * From then on no store is made and no worker is called, loads read 0, and
 * every routine returns -1 until the caller clears the fault. Writes made
 * before the fault stay. The first fault is the one kept. This is a port
 * contract (the original has none); the reference test checks each of
 * these rules natively (core_contract, docs/AREA01_MATH.md section 1).
 *
 * Arithmetic: every COP1 instruction goes through game/em_ee_float.h on bit
 * patterns (no host float operation).
 *
 * stdint only. */
#ifndef EM_AREA01_MATH_CORE_H
#define EM_AREA01_MATH_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_A01M_SPAD_BASE UINT32_C(0x70000000)
#define EM_A01M_SPAD_SIZE UINT32_C(0x4000)
/* EE RAM is mapped at 0 and at its uncached mirrors 0x20000000 and
 * 0x30000000 (up to ram_size bytes each). */
#define EM_A01M_UNCACHED UINT32_C(0x20000000)
#define EM_A01M_ACCEL UINT32_C(0x30000000)

enum {
    EM_A01M_FAULT_NONE = 0,
    EM_A01M_FAULT_NULL = 1,       /* no RAM / scratchpad view or no dispatcher */
    EM_A01M_FAULT_WORKER = 2,     /* the dispatcher returned a negative value */
    EM_A01M_FAULT_ADDRESS = 4     /* an EE address outside the mapped storage */
};

/* The callee dispatcher. `target` is the original address of the callee
 * (for an indirect call, the loaded function pointer). a[0..na) are the
 * integer argument registers a0.. and f[0..nf) the float argument registers
 * f12.. (raw bits) that the callee reads. *v0 / *f0 receive the callee's
 * results (callers that ignore a result still receive it). Returns >= 0 on
 * success; a negative value latches EM_A01M_FAULT_WORKER. */
typedef int (*EmA01MathCall)(void *ctx, uint32_t target, const uint32_t *a, unsigned na,
                             const uint32_t *f, unsigned nf, uint32_t *v0, uint32_t *f0);

typedef struct {
    uint8_t *ram;          /* EE RAM image (EE address 0) */
    uint32_t ram_size;     /* bytes (0x2000000 for the whole 32 MiB) */
    uint8_t *spad;         /* the 16 KiB scratchpad (EE address 0x70000000) */
    EmA01MathCall call;
    void *ctx;
    uint32_t fault_address; /* first fault: the EE address or callee address */
    int32_t fault_code;     /* EM_A01M_FAULT_* */
    uint32_t stores;        /* stores made through this view (counted, never read) */
    /* Optional store trace (NULL = off; the reference test uses it): each
     * store appends its EE address and size (two words) while they fit in
     * trace_cap words; a store that does not fit sets trace_len to
     * UINT32_MAX (overflowed) and nothing more is traced. */
    uint32_t *trace;
    uint32_t trace_cap;
    uint32_t trace_len;
} EmA01Math;

void em_area01_math_clear_fault(EmA01Math *m);

/* ---- storage ----------------------------------------------------------- */
/* Read-only host view of `size` bytes at EE address `a` (NULL, with a fault
 * latched, when unmapped). No alignment check: the translated routines
 * access the same aligned fields as the original (the quadword accesses of
 * 001C69A0 mask the address to 16 bytes as the original's quadword loads
 * do).
 *
 * Rule: a translation stores ONLY through em_a01m_sw / _sh / _sb (never
 * through a host pointer, never through m->ram / m->spad): those are the
 * only stores the optional trace records, and the reference test's
 * whole-memory compare relies on the trace (it also greps the translation
 * sources for direct m->ram / m->spad / em_a01m_where use). */
const uint8_t *em_a01m_where(EmA01Math *m, uint32_t a, uint32_t size);

uint32_t em_a01m_lw(EmA01Math *m, uint32_t a);
uint32_t em_a01m_lhu(EmA01Math *m, uint32_t a);
int32_t em_a01m_lh(EmA01Math *m, uint32_t a);
uint32_t em_a01m_lbu(EmA01Math *m, uint32_t a);
int32_t em_a01m_lb(EmA01Math *m, uint32_t a);
void em_a01m_sw(EmA01Math *m, uint32_t a, uint32_t v);
void em_a01m_sh(EmA01Math *m, uint32_t a, uint32_t v);
void em_a01m_sb(EmA01Math *m, uint32_t a, uint32_t v);

/* ---- workers ----------------------------------------------------------- */
/* One callee call. Returns 0, or -1 (nothing called) once a fault is
 * latched. v0 / f0 may be NULL. */
int em_a01m_call(EmA01Math *m, uint32_t target, const uint32_t *a, unsigned na, const uint32_t *f,
                 unsigned nf, uint32_t *v0, uint32_t *f0);

/* Shorthands: integer arguments only / integer arguments plus one float. */
int em_a01m_call_i(EmA01Math *m, uint32_t target, unsigned na, uint32_t a0, uint32_t a1, uint32_t a2,
                   uint32_t a3, uint32_t *v0);
int em_a01m_call_if(EmA01Math *m, uint32_t target, unsigned na, uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3, uint32_t f12, uint32_t *v0, uint32_t *f0);

static inline int em_a01m_faulted(const EmA01Math *m) { return m->fault_code != EM_A01M_FAULT_NONE; }

#ifdef __cplusplus
}
#endif

#endif
