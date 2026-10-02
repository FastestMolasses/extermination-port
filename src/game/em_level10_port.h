/* The tenth level (AREA19 entry 9's rooms -> the duct -> the pickup g[2]
 * -> back up the ladder into AREA13 -> door [17] -> button [15] -> door [8]
 * -> the lift [10] down to AREA04 entry 7): standalone translations of the
 * functions the tenth-level route census found new (decomp
 * build/s87/census/a19_delta.json and a13b_delta.json, new_functions: 13 +
 * 5 rows; TENTH_LEVEL_ROUTE.md section 5) that had no verified port
 * translation. One row is reused, not translated here: AREA19 0x8298D0,
 * whose instructions equal AREA01 0x828850's word for word
 * (em_area01_ovl_00828850, em_area01_overlay.c).
 * Docs: docs/LEVEL10_PORT.md (the census rows, what each function does, the
 * verification and the binding).
 *
 * Ground truth: the original instructions (read locally, never reproduced)
 * for every function, with the decomp's C as a guide where it is
 * byte-identical; where the decomp's C text and the instructions disagree
 * (the NEARMISS rows 00118790 and 001305B0) the instructions win. Every
 * entry is compared with the ORIGINAL code, run over recorded tenth-level
 * RAM images, by tools/test_level10_port_reference.py; docs/LEVEL10_PORT.md
 * section 3 says exactly what that covers.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian). Pointers stored in
 * records are original 32-bit addresses, resolved through `bytes` again.
 * Entries whose original (or a direct callee's) frame holds locals take
 * `sp`, the original stack pointer at entry (16-byte aligned); the frame
 * lives below it at the original's offsets.
 *
 * Calls between the translations here are direct (no hook). Every other
 * callee is a hook named by original address (boot functions and the
 * AREA19 overlay's own 0x825420 and 0x825930 at their runtime addresses);
 * pointer arguments are original addresses; a hook returns >= 0 on success
 * and its original result (when used) in *result (float results as the
 * float whose bits are the original's). The actor's +0x4C method is
 * `w_callback(ctx, function, a0)`. `ctx` is passed unchanged and never
 * dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping (fault 5, also for a misaligned
 * address), a reached NULL hook (1) or a negative hook result (2) latches
 * the first fault (address + code); from then on no hook runs, `bytes` is
 * not called, writes are dropped, *result is not written and the entry
 * returns -1. A fault latched on entry, a NULL hook table, a NULL fault
 * pointer and (entries with a result) a NULL result pointer return -1 at
 * once.
 *
 * Unbound: nothing in the port calls these entries (level side track;
 * docs/LEVEL10_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL10_PORT_H
#define EM_LEVEL10_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL10_PORT_FAULT_NONE = 0,
    EM_LEVEL10_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL10_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL10_PORT_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL10_PORT_FAULT_* */
} EmLevel10PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- callees, by original address (written from the test's HOOKS table;
     * its header_checks compares this block with HOOKS on every run) ---- */
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102738)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001031E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00131F20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00132490)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001662D0)(void *ctx, uint32_t a0);
    int (*w_001831F0)(void *ctx, int32_t n0);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t n0, int32_t n1, int32_t *result);
    int (*w_001A7B80)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001AEE10)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0F60)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1E20)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B3250)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B55E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001B6660)(void *ctx, uint32_t a0, uint32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BBDA0)(void *ctx, uint32_t a0);
    int (*w_001BBE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_001BC0E0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BC240)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BC290)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BC300)(void *ctx, uint32_t a0);
    int (*w_001C4760)(void *ctx, int32_t n0, int32_t n1, int32_t *result);
    int (*w_001C5570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result);
    int (*w_001C6120)(void *ctx, uint32_t a0, int32_t n1, uint32_t *result);
    int (*w_001C62C0)(void *ctx, uint32_t a0);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CA6E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001CD520)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint64_t q3, uint32_t a4, float f5, float f6, float f7, int32_t *result);
    int (*w_001E2BA0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3);
    int (*w_001E8B40)(void *ctx, int32_t n0);
    int (*w_001EFD90)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1, int32_t *result);
    int (*w_001F5940)(void *ctx, int32_t n0, uint32_t a1, int32_t n2);
    int (*w_001FAE70)(void *ctx, int32_t n0);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, int32_t *result);
    int (*w_001FC580)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_0021BE40)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_00825420)(void *ctx, uint32_t a0);
    int (*w_00825930)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */

    /* the actor's +0x4C method: an indirect call through the function word
     * read from memory */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel10PortHooks;

/* ---- AREA19 overlay (runtime addresses; OVERLAY/AREA19.BIN, id 16) ---- */
int em_level10_port_00823580(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_008250F0(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault);
int em_level10_port_00825240(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault);
int em_level10_port_008255D0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_008257C0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_00825AB0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_00825C70(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_00825EE0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_00826100(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_00827790(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault);
int em_level10_port_00829370(const EmLevel10PortHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                             int32_t *result, EmLevel10PortFault *fault);

/* ---- AREA13 overlay (runtime address; OVERLAY/AREA13.BIN, id 10) ---- */
int em_level10_port_008236E0(const EmLevel10PortHooks *h, EmLevel10PortFault *fault);

/* ---- boot ---- */
int em_level10_port_00118790(const EmLevel10PortHooks *h, uint32_t p, int32_t *result, EmLevel10PortFault *fault);
int em_level10_port_001305B0(const EmLevel10PortHooks *h, uint32_t self, uint32_t ent, EmLevel10PortFault *fault);
int em_level10_port_001833F0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault);
int em_level10_port_001885F0(const EmLevel10PortHooks *h, uint32_t self, int32_t *result, EmLevel10PortFault *fault);
int em_level10_port_001E6F60(const EmLevel10PortHooks *h, int32_t n0, int32_t n1, int32_t n2, int32_t n3,
                             int32_t n4, int32_t n5, int32_t n6, EmLevel10PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL10_PORT_H */
