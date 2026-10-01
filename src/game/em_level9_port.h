/* The ninth level (the lift -> AREA13 -> hatch [62] -> its ladder ->
 * AREA19 entry 9): standalone translations of the functions the a13 route
 * census found new (decomp build/s87/census/a13_delta.json, new_functions:
 * 44 rows; NINTH_LEVEL_ROUTE.md section 5) that had no verified port
 * translation. Two rows are reused, not translated here: 001E7C60
 * (em_area01_sys_001E7C60) and 00214570 (em_status_pages_00214570).
 * Docs: docs/LEVEL9_PORT.md (the census rows, what each function does, the
 * verification and the binding).
 *
 * Ground truth: the original instructions (read locally, never reproduced)
 * for every function, with the decomp's C as a guide where it is
 * byte-identical; where the decomp's C text and the instructions disagree
 * (NEARMISS, word and inline asm rows) the instructions win. Every entry is
 * compared with the ORIGINAL code, run over recorded ninth-level RAM
 * images, by tools/test_level9_port_reference.py; docs/LEVEL9_PORT.md
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
 * AREA13 overlay's own functions at their runtime addresses); pointer
 * arguments are original addresses; a hook returns >= 0 on success and its
 * original result (when used) in *result (float results as the float whose
 * bits are the original's). An indirect call through a function word (the
 * actor's +0x4C method, the AREA13 panel's step table) is
 * `w_callback(ctx, function, a0)`. `ctx` is passed unchanged and never
 * dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping (fault 5, also for a misaligned
 * address), a reached NULL hook (1), a negative hook result (2) or a value
 * the original's own code leaves undefined (7: a division by zero, whose
 * HI register the original would read) latches the first fault (address +
 * code); from then on no hook runs, `bytes` is not called, writes are
 * dropped, *result is not written and the entry returns -1. A fault
 * latched on entry, a NULL hook table, a NULL fault pointer and (entries
 * with a result) a NULL result pointer return -1 at once.
 *
 * Unbound: nothing in the port calls these entries (level side track;
 * docs/LEVEL9_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL9_PORT_H
#define EM_LEVEL9_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL9_PORT_FAULT_NONE = 0,
    EM_LEVEL9_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL9_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL9_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_LEVEL9_PORT_FAULT_REGISTER = 7       /* the original would use a value its code leaves undefined */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL9_PORT_FAULT_* */
} EmLevel9PortFault;

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
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102798)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102900)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102A60)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001031E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0011DBB8)(void *ctx, float f0, float *result);
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E520)(void *ctx, float f0, float *result);
    int (*w_0011E748)(void *ctx, float f0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_00128250)(void *ctx, float f0, int32_t *result);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_00138900)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00138C20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00139240)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001399F0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00139E00)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013A3B0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013B350)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013B9A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013BE60)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013BF20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001545B0)(void *ctx, uint32_t a0, float f1, float f2, int32_t *result);
    int (*w_00154F00)(void *ctx, uint32_t a0);
    int (*w_0015AC00)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0015AE20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001662D0)(void *ctx, uint32_t a0);
    int (*w_00174A50)(void *ctx, uint32_t a0, float f1);
    int (*w_0018C0C0)(void *ctx, uint32_t a0);
    int (*w_0018C4B0)(void *ctx, uint32_t a0, float f1, float f2);
    int (*w_0018C6A0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001916C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_00192010)(void *ctx, uint32_t a0, float f1, float f2, float f3);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t n0, int32_t n1, int32_t *result);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AEE10)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001AF780)(void *ctx, uint32_t *result);
    int (*w_001AF800)(void *ctx, uint32_t a0);
    int (*w_001AFA90)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0C60)(void *ctx, int32_t n0, int32_t n1, int32_t n2);
    int (*w_001B0D80)(void *ctx, uint32_t a0);
    int (*w_001B0F60)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1020)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t n0);
    int (*w_001B11E0)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B1630)(void *ctx, float f0, float f1, float f2, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B4810)(void *ctx, uint32_t a0);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA540)(void *ctx, uint32_t a0);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001BA8E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001BE5F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001C47A0)(void *ctx, int32_t n0, int32_t n1, int32_t *result);
    int (*w_001C6120)(void *ctx, uint32_t a0, int32_t n1, uint32_t *result);
    int (*w_001C6150)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C6160)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C62C0)(void *ctx, uint32_t a0);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C6910)(void *ctx, uint32_t a0);
    int (*w_001C7900)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3);
    int (*w_001CA5E0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_001CA6E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001CA7B0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001CA940)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001CB5B0)(void *ctx, int32_t n0);
    int (*w_001CB5F0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, uint32_t *result);
    int (*w_001CB760)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3);
    int (*w_001CD070)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001CD2B0)(void *ctx, float f0, float f1, float f2, float f3, float *result);
    int (*w_001CD520)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint64_t q3, uint32_t a4, float f5, float f6, float f7, int32_t *result);
    int (*w_001CFAE0)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4, float f5, float f6);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001CFFE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D1F20)(void *ctx, int32_t n0);
    int (*w_001D1FF0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001D2040)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001D6B10)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001D6BA0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5);
    int (*w_001D6C90)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5, int32_t n6, int32_t n7);
    int (*w_001D8BF0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001E2BA0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3);
    int (*w_001E6F60)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5, int32_t n6);
    int (*w_001EFD20)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFD90)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1, int32_t *result);
    int (*w_001EFFD0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, float f4);
    int (*w_001F02C0)(void *ctx, uint32_t a0, int32_t n1, float f2);
    int (*w_001F4A00)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F4E20)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001F5940)(void *ctx, int32_t n0, uint32_t a1, int32_t n2);
    int (*w_001F9100)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3);
    int (*w_001FB9F0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, int32_t *result);
    int (*w_0021B9A0)(void *ctx, int32_t n0, float f1, float f2);
    int (*w_00823830)(void *ctx, uint32_t a0);
    int (*w_00824160)(void *ctx, uint32_t a0);
    int (*w_00824390)(void *ctx, uint32_t a0);
    int (*w_00824520)(void *ctx, uint32_t a0);
    int (*w_00824960)(void *ctx, uint32_t a0);
    int (*w_00826610)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */

    /* an indirect call through a function word read from memory */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel9PortHooks;

/* ---- AREA13 overlay (runtime addresses; OVERLAY/AREA13.BIN, id 10) ---- */
int em_level9_port_00823700(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823940(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823A10(const EmLevel9PortHooks *h, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_00823BC0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823C10(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823D50(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823E90(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00823FE0(const EmLevel9PortHooks *h, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_00824180(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00824BB0(const EmLevel9PortHooks *h, uint32_t self, uint32_t sp, EmLevel9PortFault *fault);
int em_level9_port_00826140(const EmLevel9PortHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                            int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_008266A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00826850(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00826FB0(const EmLevel9PortHooks *h, uint32_t a0, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_00826FC0(const EmLevel9PortHooks *h, uint32_t self, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_00826FF0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00827150(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_008292A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_008293A0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_008299E0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00829AA0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);

/* ---- the AREA19 load (a13_exit) ---- */
int em_level9_port_001383C0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00138540(const EmLevel9PortHooks *h, uint32_t p, uint32_t q, EmLevel9PortFault *fault);
int em_level9_port_001386E0(const EmLevel9PortHooks *h, uint32_t self, uint32_t ent, EmLevel9PortFault *fault);
int em_level9_port_00154460(const EmLevel9PortHooks *h, uint32_t self, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_001546C0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_00154740(const EmLevel9PortHooks *h, uint32_t self, uint32_t scr, EmLevel9PortFault *fault);
int em_level9_port_001549C0(const EmLevel9PortHooks *h, uint32_t self, uint32_t scr, EmLevel9PortFault *fault);
int em_level9_port_0015A200(const EmLevel9PortHooks *h, uint32_t parent, int32_t kind, int32_t pair,
                            int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_00183440(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_001838B0(const EmLevel9PortHooks *h, uint32_t e, EmLevel9PortFault *fault);
int em_level9_port_00196970(const EmLevel9PortHooks *h, uint32_t p, uint32_t q, EmLevel9PortFault *fault);
int em_level9_port_00196CE0(const EmLevel9PortHooks *h, uint32_t self, uint32_t other, EmLevel9PortFault *fault);

/* ---- pose, animation and AREA19 actors ---- */
int em_level9_port_001BA7F0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_001BDCA0(const EmLevel9PortHooks *h, uint32_t blk, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_001BDD70(const EmLevel9PortHooks *h, uint32_t blk, int32_t *result, EmLevel9PortFault *fault);
int em_level9_port_001C06E0(const EmLevel9PortHooks *h, uint32_t e, EmLevel9PortFault *fault);
int em_level9_port_001C1030(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);
int em_level9_port_001C4BA0(const EmLevel9PortHooks *h, uint32_t self, EmLevel9PortFault *fault);

/* ---- effects ---- */
int em_level9_port_001DE920(const EmLevel9PortHooks *h, uint32_t sp, EmLevel9PortFault *fault);
int em_level9_port_001E5AC0(const EmLevel9PortHooks *h, uint32_t self, int32_t flags, int32_t seed, float dt,
                            uint32_t sp, EmLevel9PortFault *fault);
int em_level9_port_001E7050(const EmLevel9PortHooks *h, uint32_t self, uint32_t sp, EmLevel9PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL9_PORT_H */
