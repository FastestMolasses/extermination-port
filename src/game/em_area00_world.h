/* em_area00_world.h - level 3 (AREA00) side track, lane A00WORLD:
 * translations of the boot functions that AREA00 runs and neither the first
 * level nor AREA01 ran, in the census subsystems level_world, entity_logic,
 * entity_update, area_logic, math_vector, actor_anim, anim_runtime,
 * area_state, obj_registry and entity_sys (docs/AREA00_WORLD.md). Prefix
 * em_area00_world_.
 *
 * The census lists 35 such functions (../Extermination/build/s87/census/
 * a00_delta.json). 20 of them already have a verified translation in the
 * port and are reused, not translated again (docs/AREA00_WORLD.md section 1
 * names each module and its oracle). The 15 below had none.
 *
 * Hand translations of these original routines (boot ELF SCUS-97112). They
 * are named by address only: labels in decomp comments, census subsystem
 * names and FINDINGS are claims; the lines below say what the code does.
 *   00189EC0  status of a record by its type byte +3 (0, 1, 2, or for type
 *             0x0E 0 / 3 from an angle test) (NEARMISS C)
 *   00189FE0  the scratch hit record at 0x700031D4: a normalised direction,
 *             then by its kind 001B41F0 or an effect (byte-matched C)
 *   0018A180  byte +0x0A = 1, byte +0 = 2, rumble, a sound 0x17F..0x181
 *             (byte-matched C)
 *   0019AA80  segment query into 0x70003190.. then 001A7280 (NEARMISS)
 *   0019B2C0  segment set-up with a 0.01 pull-back, then 0019FE50 /
 *             0019CB60 by the flag bits (NEARMISS)
 *   0019C6F0  scan of the area's 0x28-byte trigger records for a key, then
 *             bit 30 of a collision-directory word (NEARMISS)
 *   001B0CD0  001F4F40(0) and a copy of two quadwords and one transformed
 *             quadword into the new record (byte-matched C)
 *   001B41F0  hit application by the victim's type byte +3, jump table of
 *             20 entries (NEARMISS)
 *   001C24D0  scratch point from a matrix and a vector, then 0019AD00
 *             (byte-matched C)
 *   001C5050  a quadword with its w replaced, then 001D7FA0 (asm)
 *   001C6200  per-bone reset over the +0x110 pointers (asm)
 *   001C63D0  tail call of 001C9610 (byte-matched C)
 *   001CA3B0  quaternion from three half angles (asm)
 *   001CA4D0  quaternion product into an aligned quadword (asm, words)
 *   0021BD60  a 0 / 1 test over the player record's state bytes (asm, words)
 *
 * Memory model. The routines address original EE memory (RAM, the
 * scratchpad and the stack) through caller-supplied regions keyed by
 * original address. Five of them hand the address of a stack local to a
 * callee (00189FE0, 0019B2C0, 001B0CD0, 001C5050) or build a result on the
 * stack (001CA4D0); those locals live at the original stack addresses (entry
 * `sp` less the original frame plus the original offset), so the caller must
 * map the stack region there. The saved-register slots of the original
 * frames are not written.
 *
 * Callees. Every call leaving this module goes through one worker, `call`,
 * with the callee's original address, the argument registers the callee
 * reads (a0..t3 as 64-bit register images, f12..f15 as raw bits; `na` /
 * `nf` say how many) and the stack pointer at the call. The worker writes
 * v0 / f0. Callees translated here (00189EC0 and 001B41F0 from 00189FE0)
 * are called directly.
 *
 * EE arithmetic goes through em_ee_float.h (docs/EE_FLOAT_MODEL.md).
 *
 * Fail-stop: an address outside every region, a NULL worker, a negative
 * worker result or a NULL region list latches the fault (only the first one
 * is kept: code, entry and address together, also when a worker re-enters
 * the module with the same context and faults there); the call returns -1, and while `fault` holds any value other
 * than NONE every call returns -1 before doing any work, until
 * em_area00_world_clear_fault. A NULL context returns -1 and latches
 * nothing. Bytes written before the faulting access stay written (the
 * caller discards the state). Output pointers are optional.
 *
 * Oracle: tools/test_area00_world_reference.py executes the original
 * instructions over the captured AREA00 RAM and compares, at every worker
 * call, the callee entry (stack pointer, the argument registers the callee
 * reads, the 16 bytes behind a stack pointer argument) and all of EE RAM
 * and the scratchpad against the original at the same call, then again
 * after the last store together with the result. Its build defines
 * EM_AREA00_WORLD_STORE_TRACE (em_area00_world.c) to learn which lines the
 * module stored to; ordinary builds leave it undefined.
 *
 * stdint only; no dependency on any port subsystem. */
#ifndef EM_AREA00_WORLD_H
#define EM_AREA00_WORLD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA00_WORLD_FAULT_NONE     0
#define EM_AREA00_WORLD_FAULT_NULL     1 /* NULL worker or region list */
#define EM_AREA00_WORLD_FAULT_WORKER   2 /* the worker returned < 0 */
#define EM_AREA00_WORLD_FAULT_UNMAPPED 3 /* an address outside every region */

typedef struct {
    uint32_t base;  /* original address of bytes[0] */
    uint32_t size;
    uint8_t *bytes;
} EmArea00WorldRegion;

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
} EmArea00WorldCall;

typedef int (*EmArea00WorldWorker)(void *ctx, EmArea00WorldCall *call);

typedef struct {
    const EmArea00WorldRegion *regions;
    unsigned region_count;
    EmArea00WorldWorker call;
    void *ctx;
    uint32_t sp;             /* original stack pointer at entry */
    int32_t fault;           /* EM_AREA00_WORLD_FAULT_* (latched) */
    uint32_t fault_function; /* the entry that faulted */
    uint32_t fault_address;  /* UNMAPPED: the address; NULL worker / WORKER:
                                the callee; NULL regions: 0 */
} EmArea00World;

void em_area00_world_clear_fault(EmArea00World *s);

/* Every routine returns 0, or -1 on a fault. Pointer arguments are original
 * addresses; integer arguments are the low 32 bits of the original register
 * (the module treats them as sign-extended images, as the original's callers
 * leave them). `f*` arguments are raw binary32 bits. `out` receives what the
 * original leaves in v0: the routine's own 32-bit result, or (uint64_t) the
 * full register of the last callee where the original returns it untouched
 * (001C24D0: 0019AD00's, 001C5050: 001D7FA0's, 001C63D0: 001C9610's). */
int em_area00_world_00189EC0(EmArea00World *s, uint32_t a0, int32_t *out);
int em_area00_world_00189FE0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2);
int em_area00_world_0018A180(EmArea00World *s, uint32_t a0);
int em_area00_world_0019AA80(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out);
int em_area00_world_0019B2C0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *out);
int em_area00_world_0019C6F0(EmArea00World *s, uint32_t a0, uint32_t a1, int32_t *out);
int em_area00_world_001B0CD0(EmArea00World *s, uint32_t a0, uint32_t a1);
int em_area00_world_001B41F0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t t0,
                             uint32_t t1, int32_t *out);
int em_area00_world_001C24D0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2, uint64_t *out);
int em_area00_world_001C5050(EmArea00World *s, uint32_t a0, uint32_t f12, uint64_t *out);
int em_area00_world_001C6200(EmArea00World *s, uint32_t a0);
int em_area00_world_001C63D0(EmArea00World *s, uint32_t a0, uint64_t *out);
int em_area00_world_001CA3B0(EmArea00World *s, uint32_t a0, uint32_t f12, uint32_t f13, uint32_t f14);
int em_area00_world_001CA4D0(EmArea00World *s, uint32_t a0, uint32_t a1, uint32_t a2);
int em_area00_world_0021BD60(EmArea00World *s, uint32_t a0, int32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA00_WORLD_H */
