/* em_area01_exita.h - AREA01 (level 2) side track, lane EXITA: translations
 * of the boot functions of subsystems "lowmem" and "unknown_07" that the
 * AREA01 route runs for the first time only in its exit (the area change
 * and the AREA00 arrival of beat a01_07; docs/AREA01_EXITA.md). Prefix
 * em_area01_exita_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: labels in decomp comments or FINDINGS are
 * claims, and the lines below say what the code does.
 *   00113478  0x1E gate through 00112E28, then the 0010E8A8 request with
 *             the word 0x00241D48 = 8 around it, 0010B840(D_00241D0C)
 *             and the word at 0x0027AB40 read through the uncached
 *             mirror (ee-gcc, NEARMISS C)
 *   001195A8  release of entry a0 of the 12-byte table 0x0027C6C0 when it
 *             holds 1 and none of the 48 records at 0x0027CCC0 names it
 *             (ee-gcc, byte-matched C)
 *   00128390  15 / 30 or 30 / 50 by D_0081070A and a1 (C linked from asm)
 *   00128600  byte (a0 * 16 + LCG & 15) of the table 0x00242ED0
 *             (byte-matched C)
 *   00128640  distance of the record from the point D_00810350 picks
 *             sub-state +5 through 00128600 (byte-matched C)
 *   001289C0  record / sub-block defaults, +0x34 from 00128390
 *             (byte-matched C)
 *   00128AB0  001B10B0 gate, then 001289C0 and 00102948 (byte-matched C)
 *   00129780  selector-driven probe step (13 selectors) (byte-matched C)
 *   0012A5D0  owner of a record with its sub-block at +0x1F0: states 0..4
 *             (byte-matched C)
 *   0012ADC0  001B13F0 range gate, then a heading from scratch vectors
 *             (NEARMISS C; its call passes a1 through, see the .c)
 *   0012AFC0  sub-state machine at +6: wait / turn / probe (byte-matched C)
 *   0022DCD0  owner of the table at 0x00822CF0: fill from one of 13 area
 *             tables, then per entry the 001CFB50 / 001CFBE0 requests
 *             (NEARMISS C)
 * Calls among these twelve are made directly. 001287F0 and 001B0D80 are
 * translated in em_area01_sys.c; here they are callees like any other.
 *
 * Memory model (as em_area01_sys.h). The routines address original EE
 * memory through caller-supplied regions keyed by original address (RAM,
 * the scratchpad, and a stack region). The stack region holds the frames
 * of the original layout below `sp` (the caller's stack pointer at entry):
 * 00113478 stores its ninth 0010E8A8 argument there (sp - 0x40 + 0), and
 * 0022DCD0 keeps its 0x60-byte request block at sp - 0x1A0 + 0x140, whose
 * address it hands to 001CFB50 and 001CFBE0. Every other local is a C
 * local. 00113478 reads its result word through the EE's uncached RAM
 * mirror (0x20000000 + x is RAM x); the module reads RAM x.
 *
 * Callees. Every call leaving the module goes through one worker, `call`,
 * with the callee's original address (for the indirect call of 0012A5D0,
 * the pointer the original loads from +0x4C), the argument registers the
 * original sets (a0..t3 as 64-bit register images, f12..f19 as raw bits,
 * counted by na / nf) and the stack pointer at the call. The worker writes
 * v0 (all 64 bits: the original tests the whole register where it does)
 * and f0.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result, or a NULL output pointer latches the fault (the first one
 * is kept); the call returns -1, and while `fault` is not NONE every call
 * returns -1 before doing any work, until em_area01_exita_clear_fault. A
 * NULL context returns -1 and latches nothing. Bytes written before the
 * faulting access stay written.
 *
 * Oracle: tools/test_area01_exita_reference.py runs the original
 * instructions over the captured a01_07 RAM and compares, at every worker
 * call, the callee entry and all of EE RAM and the scratchpad, then all of
 * memory and the result at the end. Its build defines
 * EM_AREA01_EXITA_STORE_TRACE to learn which lines the module stored to.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA01_EXITA_H
#define EM_AREA01_EXITA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA01_EXITA_FAULT_NONE     0
#define EM_AREA01_EXITA_FAULT_NULL     1 /* NULL context / worker / output */
#define EM_AREA01_EXITA_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA01_EXITA_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base; /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea01ExitaRegion;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;   /* original callee address */
    uint32_t sp;   /* original stack pointer at the call */
    uint64_t a[8]; /* a0..a3, t0..t3 register images */
    uint32_t f[8]; /* f12..f19 raw bits */
    uint32_t na;   /* how many of a0..t3 the original sets for this call */
    uint32_t nf;   /* how many of f12..f19 the original sets */
    uint64_t v0;   /* out: the callee's v0 (64-bit register image) */
    uint32_t f0;   /* out: the callee's f0 bits */
} EmArea01ExitaCall;

typedef int (*EmArea01ExitaWorker)(void *ctx, EmArea01ExitaCall *call);

/* Optional canonical byte view. With this set, regions are ignored, including
 * when the callback refuses an access. The original address is unchanged;
 * write is 0 for a load and 1 for a store. The returned span must remain valid
 * until that access completes. Refusal latches UNMAPPED at that address.
 * ctx is shared with call. No storage or address-mirror policy is added. */
typedef uint8_t *(*EmArea01ExitaView)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea01ExitaRegion *regions;
    unsigned region_count;
    EmArea01ExitaWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA01_EXITA_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee */
    EmArea01ExitaView view; /* optional; NULL retains the region-array path */
} EmArea01Exita;

void em_area01_exita_clear_fault(EmArea01Exita *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * the low 32 bits of v0 in *out. Integer arguments are 32-bit values the
 * original receives sign-extended in its 64-bit registers. */
int em_area01_exita_00113478(EmArea01Exita *s, int32_t *out);
int em_area01_exita_001195A8(EmArea01Exita *s, uint32_t a0, int32_t *out);
int em_area01_exita_00128390(EmArea01Exita *s, uint32_t a0, int32_t a1, int32_t *out);
int em_area01_exita_00128600(EmArea01Exita *s, int32_t a0, int32_t *out);
int em_area01_exita_00128640(EmArea01Exita *s, uint32_t a0, int32_t *out);
int em_area01_exita_001289C0(EmArea01Exita *s, uint32_t a0, uint32_t a1);
int em_area01_exita_00128AB0(EmArea01Exita *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area01_exita_00129780(EmArea01Exita *s, uint32_t a, uint32_t b, uint32_t sel, int32_t *out);
int em_area01_exita_0012A5D0(EmArea01Exita *s, uint32_t p);
int em_area01_exita_0012ADC0(EmArea01Exita *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12, int32_t *out);
int em_area01_exita_0012AFC0(EmArea01Exita *s, uint32_t a0, uint32_t a1);
int em_area01_exita_0022DCD0(EmArea01Exita *s, uint32_t p);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_EXITA_H */
