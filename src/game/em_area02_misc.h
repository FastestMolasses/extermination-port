/* em_area02_misc.h - AREA01 revisit / AREA02 (level 4) side track, lane
 * L4MISC: translations of the boot functions of census subsystems
 * weapon_equip, hud_objects, lowmem, unknown_01, unknown_03, render_vif,
 * input_io, frame_update, init_io, fx_render, frame_main, audio and
 * area_state that the revisit and AREA02 run for the first time (the
 * a02_delta census, docs/AREA02_MISC.md). Prefix em_area02_misc_.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: census subsystem names and decomp comments are
 * claims, and the lines in the .c say what the code does.
 *   0011C128  the asin-shaped kernel: polynomial and square-root forms by
 *             |x| (word assembly, read from the instructions)
 *   0011E520  its wrapper: the kernel, then the |x| > 1 error path through
 *             the four soft-float / errno workers (NEARMISS, instructions)
 *   0015C750  record +0x234 / D_00810707 clear, 0015C1F0, the +0x1C link
 *             release, 0015C7C0 (C)
 *   0015C7C0  the hurt-clip to normal-clip restart through 001749A0 (C)
 *   001A8970  the three proximity gates, the +0x34 reaction call and the
 *             follow-up commit (C; reads the caller's f20 on one path)
 *   001A8E80  the planar-distance and height gates, +0x36 = 0x14 (asm body)
 *   001A9360  the scratch distance test and the 001A91C0 response (NEARMISS)
 *   001AA640  the three box gates, +0x36 = 1 (asm body)
 *   001AA700  the D_00275B7C walk calling 001AA640 (C)
 *   001B6AE0  the three-state timer step (C)
 *   001D3A30  the 0x10-byte list record of bank D_00816D40 (C)
 *   001D3AC0  tail call 001D3A30(3, a0) (C)
 *   001D3C40  001D3A30, then the record of bank D_00816E40 (C)
 *   001D3CE0  tail call 001D3C40(3, a0) (C)
 *   001D66A0  the 33-pair Gouraud fan packet (NEARMISS, instructions)
 *   001E49F0  empty (C)
 *   001E4A00  the flash sprite pair with the VU0 colour ramps (NEARMISS)
 *   001E4CE0  the burst emitter owner, states 0..3 (NEARMISS, both jump
 *             tables read from the instructions)
 *   001EBD20  the 0x70003400 copy, its +0x34 step and one packet (NEARMISS)
 *   001EC5F0  three packets with the D_00275C34 +4 draw (NEARMISS)
 *   001EC9A0  one packet, table 0x00256D30 (NEARMISS)
 *   001ECEF0  one packet, table by D_00275C30 +0x38 (C)
 *   001EDAF0  the three records' fields, three packets, the +8 ease
 *             (NEARMISS)
 *   001F4010  the 0x90-byte ring slot of D_00275C40 (NEARMISS)
 *   001F4E40  the pulsing sprite colour (asm body)
 *   001F6B30  001F6640(001F6760()) (C)
 *   001F9660  the 0x2F0-byte copy with its +3 byte by key and area
 *             (NEARMISS)
 *   0021BD10  1 when D_008102B0 is 1 and 0021BB00 returns 0 (C linked from
 *             the assembly, checked against it)
 * Calls among these routines are direct (0011E520 -> 0011C128, 0015C750 ->
 * 0015C7C0, 001A8970 -> 0021BD10, 001AA700 -> 001AA640, 001D3AC0 /
 * 001D3C40 -> 001D3A30, 001D3CE0 -> 001D3C40, 001E4CE0 -> 001E4A00).
 * Reused, not translated here (docs/AREA02_MISC.md section 7): 0015C700
 * (em_player_0015C700), 00199FA0 (em_player_ladder_00199FA0), 001B62C0
 * (em_item_stick_sample), 001FB0B0 (em_stream_lanes_001FB0B0), 001FC520
 * (em_sfx_service_release).
 *
 * Memory model (as em_area00_low.h). The routines address original EE
 * memory through caller-supplied regions keyed by original address (RAM,
 * the scratchpad, and a stack region). The stack region holds the frames of
 * the original layout below `sp` (the caller's stack pointer at entry): the
 * locals whose addresses reach a callee live there at the original's
 * offsets (0011E520's exception record at sp - 0x60, 001E4CE0's packet
 * descriptor at sp - 0x60, 001F9660's 0x2F0-byte copy at sp - 0x2F0). Every
 * other local is a C local. 001D66A0's quadword store ignores the low four
 * address bits, as the EE does.
 *
 * Callees. Every call leaving the module goes through one worker, `call`,
 * with the callee's original address (for 001A8970's indirect call, the
 * pointer it loads from +0x34), the argument registers the original sets
 * (a0..t3 as 64-bit register images, f12..f19 as raw bits, counted by na /
 * nf) and the stack pointer at the call. The worker writes v0 (all 64 bits)
 * and f0. The tail jumps of 001D3AC0 / 001D3CE0 are direct.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md),
 * including the VU0 forms of 001E4A00's ramps.
 *
 * Registers the original reads without setting. 001A8970 reads its
 * caller's f20 on one path (state 1, opponent reaction 6 or 7): the value
 * comes from `entry_f20`, the caller's f20 bits at entry, when the binder
 * knows it; a NULL `entry_f20` there is the UNDEFINED fault. 001E4CE0 in
 * state 1 with a variant its second table does not set (0, 1, 11, 15 and
 * up) reads its caller's s0 (as a record pointer) and s5 (as a limit): the
 * module faults UNDEFINED at the first such read (address: the original
 * instruction that reads it), after doing everything before it.
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result, a NULL output pointer, an UNDEFINED read or an unmeasured
 * VU0 form latches the fault (the first one is kept); the call returns -1,
 * and while `fault` is not NONE every call returns -1 before doing any
 * work, until em_area02_misc_clear_fault. A NULL context returns -1 and
 * latches nothing. Bytes written before the faulting access stay written.
 *
 * Oracle: tools/test_area02_misc_reference.py runs the original
 * instructions over captured AREA02 / revisit RAM and compares, at every
 * worker call, the callee entry and all of EE RAM and the scratchpad, then
 * all of memory and the result at the end. Its build defines
 * EM_AREA02_MISC_STORE_TRACE to learn which lines the module stored to.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA02_MISC_H
#define EM_AREA02_MISC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA02_MISC_FAULT_NONE      0
#define EM_AREA02_MISC_FAULT_NULL      1 /* NULL context / worker / output */
#define EM_AREA02_MISC_FAULT_WORKER    2 /* the worker returned < 0 */
#define EM_AREA02_MISC_FAULT_UNMAPPED  3 /* an address outside every region */
#define EM_AREA02_MISC_FAULT_UNDEFINED 4 /* the original reads a register it never set */
#define EM_AREA02_MISC_FAULT_FLOAT     5 /* an unmeasured VU0 form (em_ee_float.h) */

typedef struct {
    uint32_t base; /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea02MiscRegion;

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
} EmArea02MiscCall;

typedef int (*EmArea02MiscWorker)(void *ctx, EmArea02MiscCall *call);

typedef struct {
    const EmArea02MiscRegion *regions;
    unsigned region_count;
    EmArea02MiscWorker call;
    void *ctx;
    uint32_t sp;               /* original stack pointer at entry */
    const uint32_t *entry_f20; /* 001A8970: the caller's f20 bits, or NULL */
    int32_t fault;             /* EM_AREA02_MISC_FAULT_* (latched) */
    uint32_t fault_function;   /* the entry that faulted */
    uint32_t fault_address;    /* UNMAPPED: the address; NULL worker / WORKER:
                                  the callee; UNDEFINED: the original
                                  instruction that reads the register */
} EmArea02Misc;

void em_area02_misc_clear_fault(EmArea02Misc *s);

/* Void routines return 0 (or -1 on a fault). Routines with a result store
 * it in *out (integers: the low 32 bits of v0; floats: raw bits). Integer
 * arguments are 32-bit values the original receives sign-extended in its
 * 64-bit registers, except where a uint64_t register image is taken. Float
 * arguments are raw bits. */
int em_area02_misc_0011C128(EmArea02Misc *s, uint32_t f12, uint32_t *out);
int em_area02_misc_0011E520(EmArea02Misc *s, uint32_t f12, uint32_t *out);
int em_area02_misc_0015C750(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_0015C7C0(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_001A8970(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001A8E80(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001A9360(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001AA640(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001AA700(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_001B6AE0(EmArea02Misc *s, uint32_t a1, uint32_t a2, int32_t *out);
int em_area02_misc_001D3A30(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001D3AC0(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_001D3C40(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001D3CE0(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_001D66A0(EmArea02Misc *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t f12,
                            uint32_t *out);
int em_area02_misc_001E49F0(EmArea02Misc *s);
int em_area02_misc_001E4A00(EmArea02Misc *s, uint32_t a0, uint32_t f12, uint32_t f13);
int em_area02_misc_001E4CE0(EmArea02Misc *s, uint32_t a0);
int em_area02_misc_001EBD20(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001EC5F0(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001EC9A0(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001ECEF0(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001EDAF0(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001F4010(EmArea02Misc *s, uint32_t a0, uint32_t a1);
int em_area02_misc_001F4E40(EmArea02Misc *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12);
int em_area02_misc_001F6B30(EmArea02Misc *s);
int em_area02_misc_001F9660(EmArea02Misc *s, uint32_t a0, uint64_t a1);
int em_area02_misc_0021BD10(EmArea02Misc *s, int32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA02_MISC_H */
