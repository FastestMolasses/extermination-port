/* em_area01_room.h - AREA01 (level 2) side track, lane A01ROOM: translations
 * of the boot functions that only the AREA01 room beats run for the first
 * time (a01_s4_east_room, a01_s5_duct, a01_s6_bridge_blocked; census field
 * room_beats_new_functions; docs/AREA01_ROOM.md). Prefix em_area01_room_.
 *
 * Twenty functions are in that census list. Thirteen already have a
 * verified translation in another port module and are re-verified over the
 * AREA01 room captures, not duplicated (docs/AREA01_ROOM.md section 4). The
 * seven below had none. Hand translations of the original routines (boot
 * ELF SCUS-97112), named by address only; census labels and decomp comments
 * are claims, and the lines below say what the code does.
 *   00188610  the halfword D_002754D8[bit 0 of byte a0+0x235], sign-extended
 *             (byte-matched C)
 *   00198D90  a0 = a record, a1 = an actor: 00102948(a0+0x10, a1+0xA0),
 *             float a0+0x14 += 3.0, 001029C0(0x70003400), 00102C58(0x70003400,
 *             0x70003400, a1+0xC0), the scratchpad words 0x70003600..0C =
 *             (0, 0, 5.0, 0), 001026A0(a0+0x20, 0x70003400, 0x70003600),
 *             001028B8(a0+0x20, a0+0x20, a0+0x10); then by the byte a0+1:
 *             0 -> a0+1 = 1, a0+2 = 0, 00102948(D_008105E0, a0+0x20),
 *             00102948(D_008105D0, a0+0x10); 1 -> when the word a1+0x230 is
 *             0x12, a0+6 = 0x0B and a0+1 = 0; then 0018C4B0(D_008105E0; float
 *             a0+0x24, 0.2), 0018C6A0(a0+0x20, D_008105E0; 0.2),
 *             00102948(D_008105D0, a0+0x10); other values: nothing more
 *             (NEARMISS C, read from the instructions)
 *   001BB400  the door-panel slide: kinds 8 / 0x16 move panel 0 by -0.2
 *             (done below -9.0); other kinds move panel 1 by -0.2 and panel
 *             2 by +0.2 (done when panel 1 is below -13.0 for kinds 0x3D /
 *             0x3E, else below -9.0). Panels are the bone records
 *             (*D_00275B40)[0..2], field +0x7C. Result 1 when done
 *             (byte-matched C)
 *   001BB7C0  1 when 001BA1F0(a0) returns non-zero, else 0 (byte-matched C)
 *   001BB7F0  when the byte D_008106B8 is zero: the panel offsets +0x7C of
 *             the same bones are zeroed (panel 0 for kinds 8 / 0x16, else
 *             panels 1 and 2), the byte a0+0x0B = 0, result 1; otherwise
 *             result 0 and nothing is written (byte-matched C)
 *   001D0D60  a0 = a record {+0 a table of 7-float rows, +4 a length, +8 a
 *             time, +0x0C a loop flag, +0x40 seven outputs}, f12 = a step:
 *             +8 += f12; with the flag it subtracts +4 until +8 < +4, else
 *             it clamps +8 to +4 - 1.0; the seven outputs are the rows at
 *             float_to_int(+8) and at the next index (0 past the end with
 *             the flag, the last row without), weighted by 1 - frac and
 *             frac (EE MULA / MADD); result 0x1000 when it wrapped or
 *             clamped, else 0 (asm C, read as code)
 *   00225A00  00121A28(D_00810040, 0, 0xD4) as a tail call (byte-matched C)
 *
 * Memory model (as em_area01_side.h / em_area01_sys.h). The routines address
 * original EE memory (RAM and the scratchpad) through caller-supplied
 * mutable regions keyed by original address.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address, the argument registers the original
 * sets (a0..a3 as 64-bit register images, f12..f15 as raw bits) and the
 * stack pointer at the call. The worker writes v0 / f0. 00225A00 jumps to
 * its callee with the stack pointer it was entered with.
 *
 * Register images. A 32-bit argument is the sign-extended 64-bit register
 * the original's callers pass. 001BB7C0 tests 001BA1F0's whole v0 register
 * against zero, so the 64-bit image from the worker is used as is.
 *
 * EE arithmetic (00198D90's add, 001BB400's adds and compares, all of
 * 001D0D60's COP1 work) goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result or a NULL output pointer latches the fault (only the first
 * one is kept); the call returns -1, and while `fault` holds any value other
 * than NONE every call returns -1 before doing any work, until
 * em_area01_room_clear_fault. A NULL context returns -1 and latches nothing.
 * Bytes written before the faulting access stay written.
 *
 * Oracle: tools/test_area01_room_reference.py executes the original
 * instructions over the captured AREA01 room-beat RAM and compares, at every
 * worker call, the callee entry and all of EE RAM and the scratchpad against
 * the original at the same call, then again after the last store together
 * with the result. Its build defines EM_AREA01_ROOM_STORE_TRACE
 * (em_area01_room.c) to learn which lines the module stored to; ordinary
 * builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA01_ROOM_H
#define EM_AREA01_ROOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA01_ROOM_FAULT_NONE     0
#define EM_AREA01_ROOM_FAULT_NULL     1 /* NULL worker / output */
#define EM_AREA01_ROOM_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA01_ROOM_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea01RoomRegion;

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
} EmArea01RoomCall;

typedef int (*EmArea01RoomWorker)(void *ctx, EmArea01RoomCall *call);
typedef uint8_t *(*EmArea01RoomView)(void *ctx, uint32_t address, uint32_t size, int write);

typedef struct {
    const EmArea01RoomRegion *regions;
    unsigned region_count;
    EmArea01RoomWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA01_ROOM_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL output: 0 */
    /* Optional canonical byte provider. When present it is authoritative:
     * a refused span faults and never falls back to the region array. */
    EmArea01RoomView view;
} EmArea01Room;

void em_area01_room_clear_fault(EmArea01Room *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * it in *out (the low 32 bits of v0). */
int em_area01_room_00188610(EmArea01Room *s, uint32_t a0, int32_t *out);
int em_area01_room_00198D90(EmArea01Room *s, uint32_t a0, uint32_t a1);
int em_area01_room_001BB400(EmArea01Room *s, uint32_t a0, int32_t *out);
int em_area01_room_001BB7C0(EmArea01Room *s, uint32_t a0, int32_t *out);
int em_area01_room_001BB7F0(EmArea01Room *s, uint32_t a0, int32_t *out);
int em_area01_room_001D0D60(EmArea01Room *s, uint32_t a0, uint32_t f12, int32_t *out);
int em_area01_room_00225A00(EmArea01Room *s);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_ROOM_H */
