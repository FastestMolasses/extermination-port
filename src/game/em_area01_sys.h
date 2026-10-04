/* em_area01_sys.h - AREA01 (level 2) side track, lane SYS: translations of
 * the boot functions of the remaining subsystems that the AREA01 main route
 * runs for the first time (docs/AREA01_SYS.md). Prefix em_area01_sys_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: labels in decomp comments or FINDINGS are
 * claims, and the lines below describe what the code does.
 *   001287F0  store a2 at halfword a1+0xF8 when it differs, then 001C67E0
 *   00128B80  +0x36 / D_0081080F gate: state bytes reset, 0012E070
 *   00128C10  owner of the 0x828A00 group records: five states, a kind /
 *             sub-state machine over 001B13F0 distance tests (NEARMISS C,
 *             followed from the original code)
 *   00157CE0  +0x0B bit-2 event dispatch into three event records
 *             (byte-matched C)
 *   00158590  scratch point (four modes) and 001F4CC0 request (NEARMISS)
 *   00158D30  placement owner, states 0..3 (NEARMISS)
 *   00159B90  placement owner with a sub-state machine over 00157CE0
 *             (NEARMISS)
 *   0015A2C0  placement owner, states 0..3, two link modes (NEARMISS)
 *   0019B4C0  scratch-segment probe over 001A06A0 / 0019CF50 (NEARMISS)
 *   0019CF50  grid cell / face walk for the scratch segment (NEARMISS)
 *   001A06A0  record hit-box walk for the scratch segment (NEARMISS)
 *   001A8840  box contact of a point with a record (byte-matched C)
 *   001A9E00  circle push-out of one record by another (byte-matched C)
 *   001AA000  circle push-out with a height test (asm C, read as code)
 *   001B0300  scene-entry record setup from D_0024D650 (byte-matched C)
 *   001B0D80  +0xB4 < -200 test (byte-matched C)
 *   001B6D70  command-record dispatcher to 001FA790 / 001FABB0 / 001FAE70 /
 *             001FB9F0 / 001FBC50 / 001FBD50 (byte-matched C)
 *   001B76D0  001B1E20(+0x14, +0x18), result 1 (byte-matched C)
 *   001E3D90  placement owner: three 001CFAE0 / 001CFBE0 requests per
 *             frame (NEARMISS)
 *   001E7CB0  area 0x13 sub-state gate (byte-matched C)
 *   001E7D20  owner of a 32 x 32 grid record: set-up, per-frame relaxation
 *             step and GS packet build (NEARMISS)
 *   001E7C60  constant fill of the grid heights (reached only in area 0x13)
 * Already translated elsewhere and only re-verified by this lane's test:
 * 00191120 (em_camera_follow_original), 001B0C00 (em_script_host_workers),
 * 001FAD70 (em_stream_lanes_original).
 *
 * Memory model. The routines address original EE memory (RAM, the
 * scratchpad, and their own stack frames where a local's address is handed
 * to a callee). The caller supplies that memory as mutable regions by
 * original address. Locals whose address escapes to a callee live in the
 * original frame layout below `sp` (the caller's original stack pointer at
 * entry), so a callee sees the very addresses the original passes.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address (for indirect calls, the pointer the
 * original loads), the argument registers the original sets (a0..a3 as
 * 64-bit register images, f12..f15 as raw bits) and the stack pointer at the
 * call. The worker writes v0 / f0. Callees translated here are called
 * directly. IOP / CD work (the stream and sound commands of 001B6D70) is
 * therefore only a request record handed to the worker: this module
 * reproduces the EE-side effects, not the IOP.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result, a NULL output pointer, or an input on which the original
 * reads a value it never set (UNDEFINED) latches the fault (only the first
 * one is kept); the call returns -1, and while `fault` holds any value
 * other than NONE every call returns -1 before doing any work, until
 * em_area01_sys_clear_fault. A NULL context returns -1 and latches nothing.
 * Bytes written before the faulting access stay written (the caller
 * discards the state). docs/AREA01_SYS.md section 4 lists which of these
 * the test checks.
 *
 * Oracle: tools/test_area01_sys_reference.py executes the original
 * instructions over the captured AREA01 RAM and compares, at every worker
 * call, the callee entry (stack pointer, the argument registers the callee
 * reads, stack locals and pointer bytes) and all of EE RAM and the
 * scratchpad against the original at the same call, then again after the
 * last store together with the return value. Its build defines
 * EM_AREA01_SYS_STORE_TRACE (em_area01_sys.c) to learn which lines the
 * module stored to; ordinary builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA01_SYS_H
#define EM_AREA01_SYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA01_SYS_FAULT_NONE     0
#define EM_AREA01_SYS_FAULT_NULL     1 /* NULL context / worker / output */
#define EM_AREA01_SYS_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA01_SYS_FAULT_UNMAPPED 3 /* an address outside every region */
#define EM_AREA01_SYS_FAULT_UNDEFINED 4 /* the original would read a value it never set */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea01SysRegion;

/* One call leaving the module. */
typedef struct {
    uint32_t fn;   /* original callee address */
    uint32_t sp;   /* original stack pointer at the call */
    uint64_t a[8]; /* a0..a3, t0..t3 register images (32-bit values sign-extended) */
    uint32_t f[4]; /* f12..f15 raw bits */
    uint32_t na;   /* how many of a0..t3 the original sets for this call */
    uint32_t nf;   /* how many of f12..f15 the original sets */
    uint64_t v0;   /* out: the callee's v0 */
    uint32_t f0;   /* out: the callee's f0 bits */
} EmArea01SysCall;

typedef int (*EmArea01SysWorker)(void *ctx, EmArea01SysCall *call);

/* Optional canonical byte view. With this set, regions are ignored, including
 * when the callback refuses an access. The original address is unchanged;
 * write is 0 for a load and 1 for a store. The returned span must remain valid
 * until that access completes. Refusal latches UNMAPPED at that address.
 * ctx is shared with call. No storage or address-mirror policy is added. */
typedef uint8_t *(*EmArea01SysView)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea01SysRegion *regions;
    unsigned region_count;
    EmArea01SysWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA01_SYS_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL output: left as it was (0
                                after em_area01_sys_clear_fault); UNDEFINED:
                                the routine that would read the unset value */
    EmArea01SysView view; /* optional; NULL retains the region-array path */
} EmArea01Sys;

void em_area01_sys_clear_fault(EmArea01Sys *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * it in *out (the low 32 bits of v0; 00191120 is not here). */
int em_area01_sys_001287F0(EmArea01Sys *s, uint32_t a0, uint32_t a1, int32_t a2, uint32_t f12);
int em_area01_sys_00128B80(EmArea01Sys *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area01_sys_00128C10(EmArea01Sys *s, uint32_t e);
int em_area01_sys_00157CE0(EmArea01Sys *s, uint32_t a0, int32_t a1, int32_t *out);
int em_area01_sys_00158590(EmArea01Sys *s, uint32_t p, int32_t a1, int32_t mode);
int em_area01_sys_00158D30(EmArea01Sys *s, uint32_t p);
int em_area01_sys_00159B90(EmArea01Sys *s, uint32_t p);
int em_area01_sys_0015A2C0(EmArea01Sys *s, uint32_t p);
int em_area01_sys_0019B4C0(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t flags, int32_t *out);
int em_area01_sys_0019CF50(EmArea01Sys *s, int32_t *out);
int em_area01_sys_001A06A0(EmArea01Sys *s, int32_t *out);
int em_area01_sys_001A8840(EmArea01Sys *s, uint32_t a0, uint32_t a1);
int em_area01_sys_001A9E00(EmArea01Sys *s, uint32_t a0, uint32_t a1);
int em_area01_sys_001AA000(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
int em_area01_sys_001B0300(EmArea01Sys *s);
int em_area01_sys_001B0D80(EmArea01Sys *s, uint32_t a0, int32_t *out);
int em_area01_sys_001B6D70(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out);
int em_area01_sys_001B76D0(EmArea01Sys *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out);
int em_area01_sys_001E3D90(EmArea01Sys *s, uint32_t p);
int em_area01_sys_001E7C60(EmArea01Sys *s, uint32_t p, uint32_t value_bits);
int em_area01_sys_001E7CB0(EmArea01Sys *s, int32_t *out);
int em_area01_sys_001E7D20(EmArea01Sys *s, uint32_t a0);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_SYS_H */
