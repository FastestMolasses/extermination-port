/* The twelfth level (AREA13's south roof, door [20] from the north, the
 * hatch [63] and AREA19 from entry 10: [7]'s first sequence, the east
 * ledge, [6]'s first stage): standalone translations of the functions the
 * twelfth-level route census found new (decomp build/s87/census/
 * a13d_delta.json and a19b_delta.json, new_functions: 9 + 30 rows;
 * TWELFTH_LEVEL_ROUTE.md section 5). None had a verified port translation.
 * Docs: docs/LEVEL12_PORT.md (the census rows, what each function does,
 * the verification and the binding).
 *
 * Ground truth: the original instructions. Every entry is compared with the
 * ORIGINAL code, run over recorded twelfth-level RAM images, by
 * tools/test_level12_port_reference.py; docs/LEVEL12_PORT.md section 3 says
 * exactly what that covers. Where the decomp's C text (NEARMISS rows) and
 * the original disagree, the original wins (the differences are listed in
 * the doc).
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian). Pointers stored in
 * records are original 32-bit addresses, resolved through `bytes` again.
 * Entries whose original (or a nested translated callee's) frame holds
 * locals take `sp`, the original stack pointer at entry (16-byte aligned);
 * the frame lives below it at the original's offsets.
 *
 * Calls between the translations here are direct (no hook). Every other
 * callee is a hook named by original address; pointer arguments are
 * original addresses; a hook returns >= 0 on success and its original
 * result (when used) in *result (float results as the float whose bits are
 * the original's; 64-bit results whole). The indirect call through a
 * function word read from memory (the actor's +0x4C method) is
 * `w_callback(ctx, function, a0)`. `ctx` is passed unchanged and never
 * dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping (fault 5, also for a misaligned
 * address), a reached NULL hook (1) or a negative hook result (2) latches
 * the first fault (address + code); from then on no hook runs, `bytes` is
 * not called, writes are dropped, *result is not written and the entry
 * returns -1. Fault 7 marks a VU0 form the float model does not define
 * (never expected: every form used is in its table). A fault latched on
 * entry, a NULL hook table, a NULL fault pointer and (entries with a
 * result) a NULL result pointer return -1 at once.
 *
 * Unbound: nothing in the port calls these entries (level side track;
 * docs/LEVEL12_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL12_PORT_H
#define EM_LEVEL12_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL12_PORT_FAULT_NONE = 0,
    EM_LEVEL12_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL12_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL12_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_LEVEL12_PORT_FAULT_REGISTER = 7       /* an unmeasured VU0 form */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL12_PORT_FAULT_* */
} EmLevel12PortFault;

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
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0011CB90)(void *ctx, float f0, float *result);
    int (*w_0011DB90)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E080)(void *ctx, float f0, int32_t *result);
    int (*w_0011E0A8)(void *ctx, uint32_t a0, float f1, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E620)(void *ctx, float f0, float f1, float *result);
    int (*w_0011E748)(void *ctx, float f0, float *result);
    int (*w_0011FD78)(void *ctx, uint32_t *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00127758)(void *ctx, uint64_t q0, float *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_00128250)(void *ctx, float f0, int32_t *result);
    int (*w_00128350)(void *ctx, float f0, uint64_t *result);
    int (*w_001749A0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_00174A50)(void *ctx, uint32_t a0, float f1);
    int (*w_00174AB0)(void *ctx, uint32_t a0);
    int (*w_001751A0)(void *ctx, uint32_t a0);
    int (*w_00175900)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001764E0)(void *ctx, uint32_t a0);
    int (*w_001796C0)(void *ctx, uint32_t a0);
    int (*w_00188610)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0019A310)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0019A6F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t n4, int32_t *result);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_0019AD00)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019AFE0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_0019B2C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0019BC40)(void *ctx, uint32_t a0);
    int (*w_0019F680)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B1380)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B13F0)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B6F00)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C6160)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C6DA0)(void *ctx, uint32_t a0);
    int (*w_001C7420)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_001C94B0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
    int (*w_001C9940)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2);
    int (*w_001CA5E0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_001CB760)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CFA60)(void *ctx, uint32_t a0, uint32_t a1, float f2, float f3);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D1F20)(void *ctx, int32_t n0);
    int (*w_001D1F80)(void *ctx, int32_t n0, int32_t n1, int32_t n2);
    int (*w_001D1FF0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001D2090)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001D2910)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001D6B10)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001D6BA0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, int32_t n5);
    int (*w_001D8C20)(void *ctx, int32_t n0);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFEB0)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001F4E20)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001FAE70)(void *ctx, int32_t n0);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_0021BE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
/* END GENERATED HOOKS */

    /* the indirect call through a function word (the actor's +0x4C method) */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel12PortHooks;

/* ---- AREA19 overlay (runtime addresses; self = the pool node) ---- */
int em_level12_port_00824BE0(const EmLevel12PortHooks *h, uint32_t self, uint32_t sp, EmLevel12PortFault *fault);
int em_level12_port_00824EF0(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault);
int em_level12_port_00824F70(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault);
int em_level12_port_00825030(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault);
int em_level12_port_008250C0(const EmLevel12PortHooks *h, EmLevel12PortFault *fault);
int em_level12_port_00825420(const EmLevel12PortHooks *h, uint32_t self, uint32_t sp, EmLevel12PortFault *fault);
int em_level12_port_00825930(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault);

/* ---- boot: the AREA19 creature family (ent = self + 0x1F0) ---- */
int em_level12_port_00138900(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault);
int em_level12_port_00138C20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault);
int em_level12_port_0013BA20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault);
int em_level12_port_0013BBB0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault);
int em_level12_port_0013BE60(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault);
int em_level12_port_0013BF20(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, EmLevel12PortFault *fault);
int em_level12_port_0013C1F0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                             EmLevel12PortFault *fault);
int em_level12_port_0013C4C0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                             EmLevel12PortFault *fault);
int em_level12_port_0013C8C0(const EmLevel12PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                             EmLevel12PortFault *fault);
int em_level12_port_0013CD50(const EmLevel12PortHooks *h, uint32_t self, uint32_t seg, int32_t *result,
                             EmLevel12PortFault *fault);
int em_level12_port_0013D220(const EmLevel12PortHooks *h, uint32_t self, int32_t *result, EmLevel12PortFault *fault);

/* ---- boot: the others ---- */
int em_level12_port_0011BCF8(const EmLevel12PortHooks *h, float x, float *result, EmLevel12PortFault *fault);
int em_level12_port_0011E420(const EmLevel12PortHooks *h, float x, uint32_t sp, float *result,
                             EmLevel12PortFault *fault);
int em_level12_port_001437E0(const EmLevel12PortHooks *h, uint32_t e, uint32_t d, EmLevel12PortFault *fault);
int em_level12_port_00146740(const EmLevel12PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, int32_t *result,
                             EmLevel12PortFault *fault);
int em_level12_port_0016EF50(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault);
int em_level12_port_00179560(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault);
int em_level12_port_0017F9E0(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault);
int em_level12_port_001821E0(const EmLevel12PortHooks *h, uint32_t a, int32_t *result, EmLevel12PortFault *fault);
int em_level12_port_001831F0(const EmLevel12PortHooks *h, int32_t n, EmLevel12PortFault *fault);
int em_level12_port_001A96F0(const EmLevel12PortHooks *h, uint32_t a, uint32_t b, EmLevel12PortFault *fault);
int em_level12_port_001B1270(const EmLevel12PortHooks *h, uint32_t obj, float px, float py, float *result,
                             EmLevel12PortFault *fault);
int em_level12_port_001B2F70(const EmLevel12PortHooks *h, uint32_t pos, uint32_t out, int32_t *result,
                             EmLevel12PortFault *fault);
int em_level12_port_001B3390(const EmLevel12PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, float f12,
                             uint32_t sp, int32_t *result, EmLevel12PortFault *fault);
int em_level12_port_001B39F0(const EmLevel12PortHooks *h, uint32_t self, uint32_t seg, uint32_t out,
                             int32_t *result, EmLevel12PortFault *fault);
int em_level12_port_001C6910(const EmLevel12PortHooks *h, uint32_t obj, EmLevel12PortFault *fault);
int em_level12_port_001C9570(const EmLevel12PortHooks *h, uint32_t m, uint32_t t, uint32_t r, uint32_t s,
                             EmLevel12PortFault *fault);
int em_level12_port_001CAFA0(const EmLevel12PortHooks *h, uint32_t a0, uint32_t a1, EmLevel12PortFault *fault);
int em_level12_port_001CB060(const EmLevel12PortHooks *h, uint32_t obj, EmLevel12PortFault *fault);
int em_level12_port_001D3F60(const EmLevel12PortHooks *h, int32_t sel, uint32_t arg, EmLevel12PortFault *fault);
int em_level12_port_001D40D0(const EmLevel12PortHooks *h, uint32_t arg, EmLevel12PortFault *fault);
int em_level12_port_001E8B40(const EmLevel12PortHooks *h, int32_t n, EmLevel12PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL12_PORT_H */
