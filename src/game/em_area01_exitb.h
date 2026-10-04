/* em_area01_exitb.h - AREA01 (level 2) side track, lane EXITB: translations
 * of the boot functions that first run in the AREA01 exit (beat a01_07,
 * after the area change to AREA00: its load and the AREA00 arrival) in the
 * census subsystems entity_logic, math_vector, hud_objects, frame_update,
 * anim_runtime and obj_registry (docs/AREA01_EXITB.md). Prefix
 * em_area01_exitb_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: labels in decomp comments, census subsystem
 * names and FINDINGS are claims; the lines below say what the code does.
 *   00156F30  owner, states 0/1/3/4: settle and copy two rig blocks, then
 *             a timed swing of both D_00275B40 records (NEARMISS C,
 *             followed from the original code, which differs from it)
 *   001576E0  +0x0B bit-2 gate: 001B6F00 request, byte +0 = 2, one of two
 *             event records through 001BA1A0 / 001BA1F0 (byte-matched C)
 *   001581A0  owner, states 0..3 over the bit (1 << +0x2E) of
 *             D_00810841[D_00810700] (NEARMISS)
 *   00158810  owner, states 0..3 with sub-state +5, 001576E0 events and a
 *             scratch timer at 0x70003B84 (NEARMISS; the original differs)
 *   00158BD0  owner, states 0..3: a two-word colour pair from the same
 *             bit, 001F4A10 request (NEARMISS)
 *   0015AB00  owner, states 0..3 over 001E8E80 / 001E9280 (byte-matched C)
 *   0015B030  owner, states 0..3 following the record at +0x20 (asm)
 *   001BB520  001B0FD0, then +0x30 = &D_002755E8, +0x34 = byte +0x2E,
 *             +0x2E = 0 (byte-matched C)
 *   001C2430  scratch segment from a point pair, 0019AB20 (byte-matched)
 *   001C2540  scratch segment pair, 0019B4C0 (asm)
 *   001C3DB0  rotation of a2 into a3 by the angle between a0 and a1 about
 *             their cross product (NEARMISS)
 *   001C6160  D_00275BF8 = 001C6120(+0x40, +0x2C); result its halfword +2
 *             (byte-matched C)
 *   001D0400  0x90-byte copy a1 -> a0, then scale of its fields by f12
 *             (asm)
 *   001E8E80  8 x 8 lattice record set-up at D_00275C18 (NEARMISS)
 *   001E9280  packet build from the D_00275C18 record (NEARMISS)
 *   001E9580  8 x 8 lattice record set-up at D_00275C1C with a per-area
 *             table (byte-matched C)
 *
 * Memory model. The routines address original EE memory (RAM and the
 * scratchpad) through caller-supplied regions keyed by original address.
 * None of them hands the address of a stack local to a callee, so the
 * stack is never addressed; `sp` is only passed on to the worker (the
 * callee's stack pointer, the entry `sp` less each frame on the way).
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address (for an indirect call, the pointer the
 * original loads), the argument registers the original sets for it that the
 * callee reads (a0..t3 as 64-bit register images, f12..f15 as raw bits;
 * `na` / `nf` say how many), and the stack pointer at the call. The worker
 * writes v0 / f0. Callees translated here (001576E0 from 00158810, 001E8E80
 * and 001E9280 from 0015AB00) are called directly.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result or a NULL context / region list latches the fault (only the
 * first one is kept); the call returns -1, and while `fault` holds any
 * value other than NONE every call returns -1 before doing any work, until
 * em_area01_exitb_clear_fault. A NULL context returns -1 and latches
 * nothing. Bytes written before the faulting access stay written (the caller
 * discards the state). Output pointers are optional (NULL = not wanted).
 *
 * Oracle: tools/test_area01_exitb_reference.py executes the original
 * instructions over the captured AREA01 exit RAM and compares, at every
 * worker call, the callee entry (stack pointer, the argument registers the
 * callee reads, the bytes behind stub pointer arguments) and all of EE RAM
 * and the scratchpad against the original at the same call, then again
 * after the last store together with the result. Its build defines
 * EM_AREA01_EXITB_STORE_TRACE (em_area01_exitb.c) to learn which lines the
 * module stored to; ordinary builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA01_EXITB_H
#define EM_AREA01_EXITB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA01_EXITB_FAULT_NONE     0
#define EM_AREA01_EXITB_FAULT_NULL     1 /* NULL worker or region list */
#define EM_AREA01_EXITB_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA01_EXITB_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea01ExitbRegion;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;   /* original callee address */
    uint32_t sp;   /* original stack pointer at the call */
    uint64_t a[8]; /* a0..a3, t0..t3 register images */
    uint32_t f[4]; /* f12..f15 raw bits */
    uint32_t na;   /* how many of a0..t3 are set (the callee's inputs) */
    uint32_t nf;   /* how many of f12..f15 are set */
    uint64_t v0;   /* out: the callee's v0 */
    uint32_t f0;   /* out: the callee's f0 bits */
} EmArea01ExitbCall;

typedef int (*EmArea01ExitbWorker)(void *ctx, EmArea01ExitbCall *call);

/* Optional canonical byte view. With this set, regions are ignored, including
 * when the callback refuses an access. The original address is unchanged;
 * write is 0 for a load and 1 for a store. The returned span must remain valid
 * until that access completes. Refusal latches UNMAPPED at that address.
 * ctx is shared with call. No storage or address-mirror policy is added. */
typedef uint8_t *(*EmArea01ExitbView)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea01ExitbRegion *regions;
    unsigned region_count;
    EmArea01ExitbWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA01_EXITB_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL regions: 0 */
    EmArea01ExitbView view; /* optional; NULL retains the region-array path */
} EmArea01Exitb;

void em_area01_exitb_clear_fault(EmArea01Exitb *s);

/* Every routine returns 0, or -1 on a fault. Pointer arguments below are
 * original addresses. `v0` / `out` outputs, where given, receive what the
 * original leaves in v0: for 001BB520 / 001C2430 / 001C2540 the full 64-bit
 * register of the last callee (001B0FD0 / 0019AB20 / 0019B4C0), which the
 * original returns untouched and a caller may test as a whole register;
 * for 001576E0 / 001C6160 the routine's own 32-bit result. */
int em_area01_exitb_00156F30(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_001576E0(EmArea01Exitb *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area01_exitb_001581A0(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_00158810(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_00158BD0(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_0015AB00(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_0015B030(EmArea01Exitb *s, uint32_t p);
int em_area01_exitb_001BB520(EmArea01Exitb *s, uint32_t a0, uint64_t *v0);
int em_area01_exitb_001C2430(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *v0);
int em_area01_exitb_001C2540(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             uint64_t *out);
int em_area01_exitb_001C3DB0(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
int em_area01_exitb_001C6160(EmArea01Exitb *s, uint32_t a0, uint32_t *out);
int em_area01_exitb_001D0400(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t f12);
int em_area01_exitb_001E8E80(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2);
int em_area01_exitb_001E9280(EmArea01Exitb *s, uint32_t a0);
int em_area01_exitb_001E9580(EmArea01Exitb *s, uint32_t a0, uint32_t a1, uint32_t a2);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_EXITB_H */
