/* em_area01_side.h - AREA01 (level 2) side track, lane SIDE: translations of
 * the boot functions that only the AREA01 side beats run for the first time
 * (docs/AREA01_SIDE.md). Prefix em_area01_side_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112), named
 * by address only: census subsystem labels and decomp comments are claims,
 * and the lines below say what the code does.
 *   001AF7C0  pop one word from the list D_00275BD0 while the halfword count
 *             D_00275BCC is positive (byte-matched C)
 *   001C4720  D_00810CB8[a0] += a1 (a byte), then the request bytes
 *             D_008106B0 = 2, D_008106B1 = a0 (byte-matched C)
 *   001CB480  six calls: 001D2910(0), 001D8C20(2), 001D2830(0, 0),
 *             001C7420(a0, 0x3F5, 1), 001D3BA0(1, word a0+0x44),
 *             001D2830(0, the 001D2910 result) (asm C, read as code)
 *   001E3D20  when bit 1 of byte a1+0 is clear and 0021BB00(D_008102B0) is 0:
 *             001EFE00(0x80000027, a1), byte a1+0x0F = 0x0C, word a0+0x1F0 =
 *             0x3C (byte-matched C)
 *   001EFE00  001EF9D0(a0, a copy of a1+0xB0 raised by 10.0 in y for the id
 *             0x80000027; f12 = 1.0); a non-zero result r gets r+0x24 =
 *             a1+0x14 and the quadwords a1+0xB0 / a1+0xC0 (asm C, read as
 *             code)
 *   001F0190  area key D_00810700 * 256 + D_00810701: 0x800 / 0x1600 send
 *             channels 2 and 3 with (1.0, 10.0) to 0021B9A0 and count one;
 *             any other key sends channel 2 (0.0, f12) when f12 is below the
 *             float (*D_00275670)+0xB8 and channel 3 (0.0, f13) when f13 is
 *             below +0xBC, counting each in D_00275C3C (byte-matched C)
 *   001F0290  0021B9A0(1; 0.0, 0.0) when D_00275C3C is non-zero
 *             (byte-matched C)
 * The other side-only functions of the census are already translated in
 * other port modules and are re-verified, not duplicated
 * (docs/AREA01_SIDE.md section 1).
 *
 * Memory model (as em_area01_sys.h). The routines address original EE
 * memory (RAM, the scratchpad, and their own stack frames where a local's
 * address is handed to a callee). The caller supplies that memory as
 * mutable regions by original address. 001EFE00's point local lives in the
 * original frame layout below `sp` (the caller's original stack pointer at
 * entry), so its callees see the address the original passes.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address, the argument registers the original
 * sets (a0..a3 as 64-bit register images, f12..f15 as raw bits) and the
 * stack pointer at the call. The worker writes v0 / f0. 001EFE00, called by
 * 001E3D20, is called directly. None of these routines makes an IOP or CD
 * call itself; whatever a callee does (0021B9A0 rewrites the record at
 * *D_00275670 in its decomp C, which is why 001F0190 reads that record again
 * after the call) belongs to the worker.
 *
 * Register images. A 32-bit argument is taken as the sign-extended 64-bit
 * register the original's callers pass. Where the original hands a callee's
 * whole v0 to another callee (001CB480's final a1) or tests it against zero
 * (001E3D20, 001EFE00), the 64-bit image from the worker is used as is.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result or a NULL output pointer latches the fault (only the first
 * one is kept); the call returns -1, and while `fault` holds any value other
 * than NONE every call returns -1 before doing any work, until
 * em_area01_side_clear_fault. A NULL context returns -1 and latches nothing.
 * Bytes written before the faulting access stay written.
 *
 * Oracle: tools/test_area01_side_reference.py executes the original
 * instructions over the captured AREA01 RAM and compares, at every worker
 * call, the callee entry and all of EE RAM and the scratchpad against the
 * original at the same call, then again after the last store together with
 * the result. Its build defines EM_AREA01_SIDE_STORE_TRACE (em_area01_side.c)
 * to learn which lines the module stored to; ordinary builds leave it
 * undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA01_SIDE_H
#define EM_AREA01_SIDE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA01_SIDE_FAULT_NONE     0
#define EM_AREA01_SIDE_FAULT_NULL     1 /* NULL worker / output */
#define EM_AREA01_SIDE_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA01_SIDE_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea01SideRegion;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;   /* original callee address */
    uint32_t sp;   /* original stack pointer at the call */
    uint64_t a[8]; /* a0..a3, t0..t3 register images */
    uint32_t f[4]; /* f12..f15 raw bits */
    uint32_t na;   /* how many of a0..t3 the original sets for this call */
    uint32_t nf;   /* how many of f12..f15 the original sets */
    uint64_t v0;   /* out: the callee's v0 (64-bit image) */
    uint32_t f0;   /* out: the callee's f0 bits */
} EmArea01SideCall;

typedef int (*EmArea01SideWorker)(void *ctx, EmArea01SideCall *call);

typedef struct {
    const EmArea01SideRegion *regions;
    unsigned region_count;
    EmArea01SideWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA01_SIDE_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL output: 0 */
    /* Optional authoritative canonical view. A refusal never falls back
     * to regions. write is zero for loads and one for stores. */
    uint8_t *(*view)(void *,uint32_t address,uint32_t size,int write);
} EmArea01Side;

void em_area01_side_clear_fault(EmArea01Side *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * it in *out (the low 32 bits of v0). */
int em_area01_side_001AF7C0(EmArea01Side *s, int32_t *out);
int em_area01_side_001C4720(EmArea01Side *s, int32_t a0, int32_t a1, int32_t *out);
int em_area01_side_001CB480(EmArea01Side *s, uint32_t a0);
int em_area01_side_001E3D20(EmArea01Side *s, uint32_t a0, uint32_t a1);
int em_area01_side_001EFE00(EmArea01Side *s, int32_t a0, uint32_t a1, int32_t *out);
int em_area01_side_001F0190(EmArea01Side *s, uint32_t f12, uint32_t f13);
int em_area01_side_001F0290(EmArea01Side *s);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_SIDE_H */
