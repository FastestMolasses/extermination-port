/* em_area02_math.h - level 4 (AREA01 revisit + AREA02) side track, lane
 * L4MATH: translations of the boot functions that the revisit and AREA02
 * run and neither the first level, AREA01 nor AREA00 ran, in the census
 * subsystems math_vector, entity_logic, actor_anim and anim_runtime
 * (docs/AREA02_MATH.md). Prefix em_area02_math_.
 *
 * The census lists 28 such functions (../Extermination/build/s87/census/
 * a02_delta.json, region boot). Four already have a verified translation in
 * the port and are reused, not translated again (00176DC0, 0017FD40,
 * 00180530, 001C47E0; docs/AREA02_MATH.md section 1 names each module and
 * its oracle). The 24 below had none.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: decomp labels, census subsystem names and
 * FINDINGS are claims; the lines below say what the code does.
 *   001575E0  script callback: 001B6F00 with a scratch vector, then the
 *             D_008105E0 / D_008105D0 points (NEARMISS C)
 *   00157B30  node use test: 001B1240 yaw, then a script by D_00810C84 /
 *             D_0081085C / D_00810858 and bits 0 / 2 of byte +0x0B
 *             (byte-matched C)
 *   00157F30  script callback: D_008106B0 = 4, D_008106D0 = word +0x14,
 *             node bytes +0x0A, +0x0B, +0 (byte-matched C)
 *   00158050  script callback: node(+0x1C)(+0x1C) position into D_008105E0
 *             (byte-matched C)
 *   001582E0  node behaviour: a room-mask bit and a one-shot sound
 *             (NEARMISS C)
 *   00158EC0  node behaviour with a use sub-machine (byte-matched C)
 *   00159620  node behaviour with a use sub-machine, 00157B30 (byte-matched C)
 *   00159970  node behaviour with a use sub-machine (NEARMISS C)
 *   00159E70  node behaviour: an item-count flag sets a room-mask bit
 *             (byte-matched C)
 *   00183C40  a node's aim point by its type bytes +2 / +3 (byte-matched C)
 *   001B18F0  four 001B1630 probes of +-a1 and +-a2 through the node
 *             matrix (NEARMISS C)
 *   001BC960  node behaviour, a door pair by the room mask (byte-matched C)
 *   001BD560  node behaviour, two jump-table sub-machines (NEARMISS C)
 *   001BDFC0  node behaviour, a door pair by the story bytes (byte-matched C)
 *   001C48C0  node behaviour, states 0..3 (asm, words)
 *   001C4960  node behaviour, states 0..3 with 001B1020 (asm, words)
 *   001C4AF0  node behaviour, states 0..3 with 001CA5E0 (asm, words)
 *   001C4CB0  node behaviour emitting 001F4E40 points (byte-matched C)
 *   001C7EB0  node behaviour following the pose D_00275B40 (NEARMISS C)
 *   001C8140  spawn of a class-0x0C node running 001C7EB0 (byte-matched C)
 *   001CAE40  draw method: 001CA7B0 test, 001D3AC0 / 001D3CE0, a display
 *             record, 001CAAC0 (byte-matched C)
 *   001CAF60  tail call of 001CAE40 with word +0x44 (byte-matched C)
 *   001CB4F0  draw method: a fixed call sequence around 001D38F0 (asm)
 *   001CB580  tail call of 001CB4F0 with word +0x44 (byte-matched C)
 *
 * Memory model. The routines address original EE memory (RAM and the
 * scratchpad) through caller-supplied regions keyed by original address.
 * None of them hands a stack address to a callee, so no stack region is
 * needed; `sp` is kept only to report the stack pointer each callee sees.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address (for a node method or a callback: the
 * loaded pointer), the argument registers the original sets up for that call
 * (a0..a3 as 64-bit register images, f12..f14 as raw bits; `na` / `nf` say
 * how many) and the stack pointer at the call. The worker writes v0 / f0.
 * Calls between routines of this module (00159620 -> 00157B30, 001CAF60 ->
 * 001CAE40, 001CB580 -> 001CB4F0) are direct. Where the original relies on
 * a register a callee leaves unchanged (001BD560 after 001BD270 / 001BD460,
 * docs/AREA02_MATH.md section 3) the translation uses the value the register
 * held before the call; the reference test checks statically that the
 * callee never writes it.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result or a NULL region list latches the fault (only the first one
 * is kept: code, entry and address together, also when a worker re-enters
 * the module with the same context and faults there); the call returns -1,
 * and while `fault` holds any value other than NONE every call returns -1
 * before doing any work, until em_area02_math_clear_fault. A NULL context
 * returns -1 and latches nothing. Bytes written before the faulting access
 * stay written (the caller discards the state). Output pointers are
 * optional.
 *
 * Oracle: tools/test_area02_math_reference.py executes the original
 * instructions over the captured revisit / AREA02 RAM and compares, at every
 * worker call, the callee entry (stack pointer and the argument registers)
 * and all of EE RAM and the scratchpad against the original at the same
 * call, then again after the last store together with the result. Its build
 * defines EM_AREA02_MATH_STORE_TRACE (em_area02_math.c) to learn which lines
 * the module stored to; ordinary builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA02_MATH_H
#define EM_AREA02_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA02_MATH_FAULT_NONE     0
#define EM_AREA02_MATH_FAULT_NULL     1 /* NULL worker or region list */
#define EM_AREA02_MATH_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA02_MATH_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea02MathRegion;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;   /* original callee address (or the loaded method pointer) */
    uint32_t sp;   /* original stack pointer at the call */
    uint64_t a[4]; /* a0..a3 register images */
    uint32_t f[4]; /* f12..f15 raw bits */
    uint32_t na;   /* how many of a0..a3 are set */
    uint32_t nf;   /* how many of f12..f15 are set */
    uint64_t v0;   /* out: the callee's v0 */
    uint32_t f0;   /* out: the callee's f0 bits */
} EmArea02MathCall;

typedef int (*EmArea02MathWorker)(void *ctx, EmArea02MathCall *call);

typedef struct {
    const EmArea02MathRegion *regions;
    unsigned region_count;
    EmArea02MathWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA02_MATH_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL regions: 0 */
} EmArea02Math;

void em_area02_math_clear_fault(EmArea02Math *s);

/* Every routine returns 0, or -1 on a fault. Pointer arguments are original
 * addresses; integer arguments are the low 32 bits of the original register
 * (treated as sign-extended images, as the original's callers leave them).
 * `out` receives the routine's own 32-bit result where the original returns
 * one. The node behaviours and draw methods are void in the original. */
int em_area02_math_001575E0(EmArea02Math *s, uint32_t a0, int32_t *out);
int em_area02_math_00157B30(EmArea02Math *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area02_math_00157F30(EmArea02Math *s, uint32_t a0, int32_t *out);
int em_area02_math_00158050(EmArea02Math *s, uint32_t a0, int32_t *out);
int em_area02_math_001582E0(EmArea02Math *s, uint32_t a0);
int em_area02_math_00158EC0(EmArea02Math *s, uint32_t a0);
int em_area02_math_00159620(EmArea02Math *s, uint32_t a0);
int em_area02_math_00159970(EmArea02Math *s, uint32_t a0);
int em_area02_math_00159E70(EmArea02Math *s, uint32_t a0);
int em_area02_math_00183C40(EmArea02Math *s, uint32_t a0, uint32_t a1);
int em_area02_math_001B18F0(EmArea02Math *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out);
int em_area02_math_001BC960(EmArea02Math *s, uint32_t a0);
int em_area02_math_001BD560(EmArea02Math *s, uint32_t a0);
int em_area02_math_001BDFC0(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C48C0(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C4960(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C4AF0(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C4CB0(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C7EB0(EmArea02Math *s, uint32_t a0);
int em_area02_math_001C8140(EmArea02Math *s, uint32_t a0, uint32_t a1, uint32_t a2);
int em_area02_math_001CAE40(EmArea02Math *s, uint32_t a0, uint32_t a1);
int em_area02_math_001CAF60(EmArea02Math *s, uint32_t a0);
int em_area02_math_001CB4F0(EmArea02Math *s, uint32_t a0, uint32_t a1);
int em_area02_math_001CB580(EmArea02Math *s, uint32_t a0);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA02_MATH_H */
