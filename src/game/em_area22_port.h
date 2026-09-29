/* AREA22 boot functions and the AREA22 area init: standalone translations
 * of the functions the AREA22 route census found new (decomp
 * build/s87/census/a22_delta.json, new_functions: 11 boot rows) that had no
 * port translation, plus the AREA22 overlay's area init at runtime 0x823580
 * (OVERLAY/AREA22.BIN header id 0x13, link overlay_AREA22_func_00823540).
 * Docs: docs/AREA22_PORT.md.
 *
 * Translated here (8 census rows and the init):
 *   00100110  the sign of the soft-float compare 001274B0 (a0..a3 passed on)
 *   001028E8  one VU0 multiply of two quadwords (vmul.xyzw)
 *   0012D940  sub-state of the 0012A5D0 bug nodes (their +5 = 10 / 11)
 *   00183010  moves an actor by a vector: +0xA0, +0xB0, 0x70003B40, then
 *             0x70003B50 = +0xC0
 *   0018C850  chases a vector's y toward a target by a capped step
 *   0018C920  chases a vector's x and z toward another's by a capped step
 *   001963A0  camera action 12 (the camera block 0x8101E0, the player)
 *   001BB310  script op09 callback of the door program 0x24DA40: points the
 *             camera target and eye (D_008105E0 / D_008105D0)
 *   0x823580  AREA22 area init (called by 001E7780 for area key 0x1600)
 * The other three census rows have verified translations elsewhere and are
 * reused, not duplicated: 001A44B0 (em_coll_probe_original.c), 001A4830
 * (em_coll_move_original.c) and 001A5C30 (em_coll_segment_walkers.c);
 * tools/test_area22_port_reference.py re-checks them on the AREA22 drum
 * prims (docs/AREA22_PORT.md section 3).
 *
 * Ground truth: the decomp's C where it is byte-identical (00100110,
 * 0012D940, 00183010, 0018C920, 001BB310, the init), the original
 * instructions for 001028E8 (inline asm in the decomp), 0018C850 (word
 * asm) and 001963A0 (NEARMISS C, checked against the instructions), read
 * locally; the load/store order and the float operand order of every
 * function follow the original instructions. Every entry is compared with
 * the ORIGINAL code, run over recorded AREA22 RAM images, by
 * tools/test_area22_port_reference.py (callee calls and arguments, memory
 * at every call entry, the memory accesses between calls one for one,
 * final RAM and scratchpad, return values). docs/AREA22_PORT.md section 3
 * says exactly what that covers; nothing beyond it is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian, original offsets).
 * Pointers stored in records are original 32-bit addresses and are resolved
 * through `bytes` again where the original dereferences them. 001028E8's
 * quadword addresses are aligned down to 16 as the EE does.
 *
 * Calls between the translations: 001963A0 calls 0018C920 and 0018C850
 * directly (no hook); every function is also a public entry.
 *
 * Callees: every other boot function is a hook named by original address.
 * Pointer arguments are original addresses. A hook returns >= 0 on
 * success; its original result (when the original uses one) goes to
 * *result (float results as the float whose bits are the original's;
 * 001274B0's as the whole 64-bit register). The module never simulates a
 * callee. `ctx` is passed unchanged to `bytes` and every hook and never
 * dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * an entry with a result does not write *result, and the entry returns -1.
 * A fault already latched on entry makes the call return -1 without doing
 * anything; so do a NULL hook table, a NULL fault pointer and (for the
 * entries with a result) a NULL result pointer. A VU0 form em_ee_float.h
 * refuses (never, for the one form 001028E8 uses) latches code 6.
 *
 * Unbound: nothing in the port calls these entries yet (level-3/4 side
 * track; docs/AREA22_PORT.md "Binding").
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA22_PORT_H
#define EM_AREA22_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_AREA22_PORT_100110 0x00100110u /* 001274B0(a0..a3) > 0 */
#define EM_AREA22_PORT_1028E8 0x001028E8u /* out = a * b, four lanes (VU0) */
#define EM_AREA22_PORT_12D940 0x0012D940u /* 0012A5D0 bug node, +5 = 10 / 11 */
#define EM_AREA22_PORT_183010 0x00183010u /* move an actor by a vector */
#define EM_AREA22_PORT_18C850 0x0018C850u /* y chase, returns 4 on a snap */
#define EM_AREA22_PORT_18C920 0x0018C920u /* x / z chase, returns bits 1 / 2 on a snap */
#define EM_AREA22_PORT_1963A0 0x001963A0u /* camera action 12 */
#define EM_AREA22_PORT_1BB310 0x001BB310u /* op09 callback: camera target and eye; returns 1 */
#define EM_AREA22_PORT_823580 0x00823580u /* AREA22 area init */

enum {
    EM_AREA22_PORT_FAULT_NONE = 0,
    EM_AREA22_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA22_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA22_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_AREA22_PORT_FAULT_FLOAT_FORM = 6     /* em_ee_float.h refused a VU0 form */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the VU0 instruction's function */
    int32_t code;     /* EM_AREA22_PORT_FAULT_* */
} EmArea22PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees, by original address -------------------------- */
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_0012E070)(void *ctx, uint32_t a0);
    int (*w_001274B0)(void *ctx, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t *result);
    int (*w_00128640)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001287F0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, float f3);
    int (*w_00128830)(void *ctx, uint32_t a0, float f1, float f2, float f3);
    int (*w_0018C0C0)(void *ctx, uint32_t a0);
    int (*w_0018C4B0)(void *ctx, uint32_t a0, float f1, float f2);
    int (*w_0018C6A0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001C2770)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_001C3D60)(void *ctx, uint32_t a0, uint32_t a1);
} EmArea22PortHooks;

/* Entries without a result: 0, or -1 on a fault. */
int em_area22_port_001028E8(const EmArea22PortHooks *h, uint32_t out, uint32_t a, uint32_t b,
                            EmArea22PortFault *fault);
int em_area22_port_0012D940(const EmArea22PortHooks *h, uint32_t self, uint32_t sub, EmArea22PortFault *fault);
int em_area22_port_00183010(const EmArea22PortHooks *h, uint32_t actor, uint32_t delta, EmArea22PortFault *fault);
int em_area22_port_001963A0(const EmArea22PortHooks *h, uint32_t cam, uint32_t player, EmArea22PortFault *fault);
int em_area22_port_00823580(const EmArea22PortHooks *h, EmArea22PortFault *fault);

/* Entries with a result: *result receives the original return value; it is
 * left unwritten when the call returns -1. 00100110 passes its four
 * argument registers (whole 64-bit values) to 001274B0 unchanged. */
int em_area22_port_00100110(const EmArea22PortHooks *h, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                            int32_t *result, EmArea22PortFault *fault);
int em_area22_port_0018C850(const EmArea22PortHooks *h, uint32_t vec, float target, float step, int32_t *result,
                            EmArea22PortFault *fault);
int em_area22_port_0018C920(const EmArea22PortHooks *h, uint32_t goal, uint32_t vec, float step, int32_t *result,
                            EmArea22PortFault *fault);
int em_area22_port_001BB310(const EmArea22PortHooks *h, uint32_t actor, int32_t *result, EmArea22PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA22_PORT_H */
