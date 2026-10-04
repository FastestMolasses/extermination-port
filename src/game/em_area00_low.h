/* FIRST-LEVEL DEPENDENCY (chain step AIMCAM, 2026-10-01): 00102870 below
 * is the one bound owner of that original on the first level's aim path
 * (the beam 001E2BA0's divide, em_aim_fire_runtime; AIM_FIRE.md section
 * 7). A change here changes AREA11: re-run make test-area00-low-reference.
 *
 * em_area00_low.h - AREA00 (level 3) side track, lane A00LOW: translations
 * of the boot functions of census subsystems lowmem, unknown_07,
 * unknown_02, init_io, input_io, stream_archive, audio and movie that the
 * AREA00 route runs for the first time (the a00_delta census,
 * docs/AREA00_LOW.md). Prefix em_area00_low_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: census subsystem names and decomp comments are
 * claims, and the lines in the .c say what the code does.
 *   001000C0  sign of 001274B0's result, a0..a3 passed through (C)
 *   00102870  VU0: a0.xyz = a1.xyz * (1 / f12), a0.w = a1.w (instructions)
 *   00102990  VU0: a0 = VFTOI0 of a1, all four lanes (instructions)
 *   001181B0  event step over the 48 records of 0x6A bytes at 0x0027CCC0
 *             (ee-gcc, NEARMISS C checked against the instructions)
 *   00119080  byte interpolation a + (b - a) * d / c (ee-gcc, C)
 *   00128830  a0 +0xB0.. += the (f12, f13, f14) offset turned by a0 +0xD0
 *   001288D0  two scratch blocks for 001F4A00, by a1 +0xE1
 *   00129F00  a1 +0xF0 fall step and the 0019AB20 probe
 *   00129FC0  reaction owner, states 0..7 of a0 +5 (jump table)
 *   0012B410  sub-state machine at a0 +6, states 0..4 (NEARMISS)
 *   0012B970  sub-state machine at a0 +6, states 0..5 (jump table)
 *   0012BE20  sub-state machine at a0 +6, states 0..7 (jump table)
 *   0012C490  sub-state machine at a0 +6, states 0..8 (jump table)
 *   0012CAA0  sub-state machine at a0 +6, states 0..2, then the 0x70003B8D
 *             / D_008106BC reset
 *   0012D240  the 0x70003B8D / D_008106BC reset, then states 0..1
 *             (NEARMISS)
 *   0012D580  sub-state machine at a0 +7, states 0..3
 *   0012D850  sub-state machine at a0 +6, states 0..1 (word assembly)
 *   0012DE90  scale step of the two matrices behind D_00275B40 (NEARMISS)
 *   0012E070  release of bit (a0 +0xF6 & 7) of D_0081083C
 *   0012E0B0  claim of a free bit of D_0081083C by the heading difference
 *   0012E260  heading step toward D_00810350 / D_00810358
 *   0012E2C0  sub-state machine at a0 +7, the turn toward a1 +0xE8
 *   00198CE0  camera record step, states 0..1 of a0 +1
 *   00198F10  camera record step, states 0..1 of a0 +1 (NEARMISS)
 *   001B5360  down probe through 0019A570 and the 001F9100 / 001F9180
 *             correction by a0 +3 (jump table)
 *   001B7670  0x70003B91 latch by the mode word a2 +8 (instructions)
 *   001B7700  D_008106CE / D_008106CF request by the mode word a2 +8
 *             (NEARMISS)
 *   001B8AB0  shake of D_008105D0 / D_008105E0 by the mode word a2 +8
 *             (NEARMISS)
 *   001E7310  channel-9 owner, states 0..3 of a0 +4
 *   001FC580  per-sound request slot of the tables at 0x00281F30
 *   001FF030  D_00810701 store and the tail call 001FF080(0, 0x1D)
 * Calls among these routines are direct. Functions translated by other
 * modules (00113478, 00128600, 00128640, 0012A5D0, 0012ADC0, 0012AFC0,
 * 0022DCD0: em_area01_exita; 00193D90: em_camera_leftovers) are callees
 * like any other here.
 *
 * Memory model (as em_area01_exita.h). The routines address original EE
 * memory through caller-supplied regions keyed by original address (RAM,
 * the scratchpad, and a stack region). The stack region holds the frames
 * of the original layout below `sp` (the caller's stack pointer at entry):
 * 001FC580 hands 001FBF50 the addresses of its two stack words sp - 0x40 +
 * 0x38 and + 0x3C and reads them back. Every other local is a C local.
 * 00102870 and 00102990 move whole quadwords: the EE ignores the low four
 * address bits, and so does the module.
 * The one canonical progress byte these routines use, D_0081083C (the
 * player's grab-slot bits, census L01, EmProgress in em_scene_state.h), is
 * not addressed through the regions: 0012E070 and 0012E0B0 read and write
 * it through `grab_bits`, a view the binder points at the canonical byte
 * (SCENE_COORDINATOR_DESIGN.md 3.2). A NULL view they reach is a NULL
 * fault with address 0.
 *
 * Callees. Every call leaving the module goes through one worker, `call`,
 * with the callee's original address (for the indirect call of 00129FC0,
 * the pointer the original loads from +0x4C), the argument registers the
 * original sets (a0..t3 as 64-bit register images, f12..f19 as raw bits,
 * counted by na / nf) and the stack pointer at the call. The worker writes
 * v0 (all 64 bits: the original tests the whole register where it does)
 * and f0. 001FF030's tail jump to 001FF080 is a call with 001FF030's own
 * entry stack pointer.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Optional `view`: an access no region holds is asked of the binder's view
 * (per access, with its read / store intent); NULL leaves it unmapped.
 *
 * Fail-stop: an address outside every region (and the view), a NULL worker, a negative
 * worker result, a NULL output pointer, or the divide-by-zero trap of
 * 00119080 latches the fault (the first one is kept); the call returns -1,
 * and while `fault` is not NONE every call returns -1 before doing any work,
 * until em_area00_low_clear_fault. A NULL context returns -1 and latches
 * nothing. Bytes written before the faulting access stay written.
 *
 * Oracle: tools/test_area00_low_reference.py runs the original
 * instructions over captured AREA00 RAM and compares, at every worker
 * call, the callee entry and all of EE RAM and the scratchpad, then all of
 * memory and the result at the end. Its build defines
 * EM_AREA00_LOW_STORE_TRACE to learn which lines the module stored to.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA00_LOW_H
#define EM_AREA00_LOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA00_LOW_FAULT_NONE     0
#define EM_AREA00_LOW_FAULT_NULL     1 /* NULL context / worker / output */
#define EM_AREA00_LOW_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA00_LOW_FAULT_UNMAPPED 3 /* an address outside every region */
#define EM_AREA00_LOW_FAULT_TRAP     4 /* the original's divide-by-zero trap */

typedef struct {
    uint32_t base; /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea00LowRegion;

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
} EmArea00LowCall;

typedef int (*EmArea00LowWorker)(void *ctx, EmArea00LowCall *call);

/* Optional memory beyond the regions: `size` bytes at `address` for a read
 * (write 0) or a store (write 1), or NULL (unmapped). Asked per access,
 * only when no region holds the span (the AREA01 binder's checked views). */
typedef uint8_t *(*EmArea00LowView)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea00LowRegion *regions;
    unsigned region_count;
    EmArea00LowWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    uint8_t *grab_bits;      /* view of the canonical grab-slot bits byte
                                (0012E070 / 0012E0B0); not a region address */
    int32_t fault;           /* EM_AREA00_LOW_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; TRAP: the dividing routine */
    EmArea00LowView view;    /* optional: after the regions (NULL: none) */
    void *view_ctx;
} EmArea00Low;

void em_area00_low_clear_fault(EmArea00Low *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * the low 32 bits of v0 in *out. Integer arguments are 32-bit values the
 * original receives sign-extended in its 64-bit registers, except where a
 * uint64_t register image is taken. Float arguments are raw bits. */
int em_area00_low_001000C0(EmArea00Low *s, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, int32_t *out);
int em_area00_low_00102870(EmArea00Low *s, uint32_t a0, uint32_t a1, uint32_t f12);
int em_area00_low_00102990(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_001181B0(EmArea00Low *s, uint32_t ev);
int em_area00_low_00119080(EmArea00Low *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, int32_t *out);
int em_area00_low_00128830(EmArea00Low *s, uint32_t a0, uint32_t f12, uint32_t f13, uint32_t f14);
int em_area00_low_001288D0(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_00129F00(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_00129FC0(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012B410(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012B970(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012BE20(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012C490(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012CAA0(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012D240(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012D580(EmArea00Low *s, uint32_t a0, uint32_t a1, uint64_t a2);
int em_area00_low_0012D850(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_0012DE90(EmArea00Low *s, uint32_t a0);
int em_area00_low_0012E070(EmArea00Low *s, uint32_t a0);
int em_area00_low_0012E0B0(EmArea00Low *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area00_low_0012E260(EmArea00Low *s, uint32_t a0);
int em_area00_low_0012E2C0(EmArea00Low *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area00_low_00198CE0(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_00198F10(EmArea00Low *s, uint32_t a0, uint32_t a1);
int em_area00_low_001B5360(EmArea00Low *s, uint32_t a0);
int em_area00_low_001B7670(EmArea00Low *s, uint32_t a2, int32_t *out);
int em_area00_low_001B7700(EmArea00Low *s, uint32_t a1, uint32_t a2, int32_t *out);
int em_area00_low_001B8AB0(EmArea00Low *s, uint32_t a1, uint32_t a2, int32_t *out);
int em_area00_low_001E7310(EmArea00Low *s, uint32_t a0);
int em_area00_low_001FC580(EmArea00Low *s, uint32_t a0, int32_t a1);
int em_area00_low_001FF030(EmArea00Low *s, uint32_t a0);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA00_LOW_H */
