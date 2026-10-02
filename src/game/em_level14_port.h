/* The fourteenth level (AREA19's hall, sub 0's west part, the stair tower
 * and AREA15: the a19d / a15 route): standalone translations of the
 * functions the fourteenth-level route census found new (decomp
 * build/s87/census/a19d_delta.json and a15_delta.json, new_functions;
 * FOURTEENTH_LEVEL_ROUTE.md section 5: 59 functions that ran in no earlier
 * pass) that had no verified port translation (40 of them; the other 19
 * are reused from the verified player-closure, ladder-climb, aim / fire,
 * player-major2 and actor-collision modules). Docs: docs/LEVEL14_PORT.md
 * (the census rows, what each function does, the verification and the
 * binding).
 *
 * Ground truth: the original instructions. Every entry is compared with the
 * ORIGINAL code, run over recorded fourteenth-level RAM images, by
 * tools/test_level14_port_reference.py; docs/LEVEL14_PORT.md section 3 says
 * exactly what that covers. Where the decomp's C text (NEARMISS rows) and
 * the original disagree, the original wins (the differences are listed in
 * the doc).
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian). Pointers stored in
 * records are original 32-bit addresses, resolved through `bytes` again.
 * Entries whose original frame holds locals take `sp`, the original stack
 * pointer at entry (16-byte aligned); the frame lives below it at the
 * original's offsets.
 *
 * Calls between the translations here are direct (no hook). Every other
 * callee is a hook named by original address; pointer arguments are
 * original addresses; a hook returns >= 0 on success and its original
 * result (when used) in *result (float results as the float whose bits are
 * the original's). The indirect call through a function word read from
 * memory (the actor's +0x4C method) is `w_callback(ctx, function, a0)`.
 * `ctx` is passed unchanged and never dereferenced.
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
 * docs/LEVEL14_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL14_PORT_H
#define EM_LEVEL14_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL14_PORT_FAULT_NONE = 0,
    EM_LEVEL14_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL14_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL14_PORT_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL14_PORT_FAULT_* */
} EmLevel14PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- callees, by original address (written from the test's HOOKS table;
     * its header_checks compares this block with HOOKS on every run) ---- */
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E748)(void *ctx, float f0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_00131940)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_00132490)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001328D0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00133A20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00153B50)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00153EA0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001749A0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_001885B0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_00199DB0)(void *ctx, uint32_t a0);
    int (*w_00199FA0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0019BC40)(void *ctx, uint32_t a0);
    int (*w_0019C6F0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AEE10)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0CD0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t n0);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B1630)(void *ctx, float f0, float f1, float f2, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001B6660)(void *ctx, uint32_t a0, uint32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA540)(void *ctx, uint32_t a0);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001BA8E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C4760)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C4820)(void *ctx, uint32_t a0);
    int (*w_001C5C90)(void *ctx, uint32_t a0);
    int (*w_001C6160)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CA5F0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001CA6F0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001CFB50)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4, float f5, float f6, float f7);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001EFD90)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1, uint32_t *result);
    int (*w_001F5940)(void *ctx, int32_t n0, uint32_t a1, int32_t n2);
    int (*w_001F66F0)(void *ctx, uint32_t a0);
    int (*w_001FAE70)(void *ctx, int32_t n0);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_001FC3C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, float f3, float f4);
    int (*w_001FC520)(void *ctx, uint32_t a0);
    int (*w_0021C040)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00823B40)(void *ctx, uint32_t a0);
    int (*w_00823C80)(void *ctx, uint32_t a0);
    int (*w_00824240)(void *ctx, uint32_t a0);
    int (*w_00824350)(void *ctx, uint32_t a0);
    int (*w_008243E0)(void *ctx, uint32_t a0);
    int (*w_00824B40)(void *ctx, uint32_t a0);
    int (*w_00824C90)(void *ctx, uint32_t a0);
/* END GENERATED HOOKS */

    /* the indirect call through a function word (the actor's +0x4C method) */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel14PortHooks;

typedef EmLevel14PortHooks H14;
typedef EmLevel14PortFault F14;

/* ---- AREA15 overlay (runtime addresses; self = the pool node) ---- */
int em_level14_port_00823580(const H14 *h, F14 *fault);
int em_level14_port_008235A0(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_008236B0(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00823780(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00823850(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_008239F0(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00823E40(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824070(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824510(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824560(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_008247B0(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824990(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824E00(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00824E50(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00825030(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_008252D0(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00825320(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00825430(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00825D10(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00826600(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00826850(const H14 *h, uint32_t self, F14 *fault);

/* ---- AREA19 overlay: two script callbacks (both return 1) ---- */
int em_level14_port_00826B30(const H14 *h, uint32_t self, int32_t *result, F14 *fault);
int em_level14_port_00827540(const H14 *h, uint32_t a0, int32_t *result, F14 *fault);

/* ---- boot: the 0012E3A0 creatures' state handlers (ent = self + 0x1F0)
 * and the AREA15 enemy 00153950 ---- */
int em_level14_port_00131650(const H14 *h, uint32_t self, uint32_t ent, F14 *fault);
int em_level14_port_00131740(const H14 *h, uint32_t self, uint32_t ent, F14 *fault);
int em_level14_port_00131B10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault);
int em_level14_port_00131E80(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00131F20(const H14 *h, uint32_t self, uint32_t a1, uint32_t a2, F14 *fault);
int em_level14_port_001339E0(const H14 *h, uint32_t a0, uint32_t ent, F14 *fault);
int em_level14_port_00153950(const H14 *h, uint32_t self, F14 *fault);
int em_level14_port_00153A10(const H14 *h, uint32_t self, uint32_t ent, F14 *fault);
int em_level14_port_00153A90(const H14 *h, uint32_t self, uint32_t out, F14 *fault);

/* ---- boot: the others ---- */
int em_level14_port_0016AC50(const H14 *h, uint32_t p, F14 *fault);
int em_level14_port_001782A0(const H14 *h, uint32_t p, uint32_t sp, int32_t *result, F14 *fault);
int em_level14_port_001AA2A0(const H14 *h, uint32_t p, uint32_t object, float radius, int32_t *result,
                             F14 *fault);
int em_level14_port_001AA410(const H14 *h, uint32_t object, float *result, F14 *fault);
int em_level14_port_001B2D00(const H14 *h, uint32_t a0, uint32_t a1, int32_t *result, F14 *fault);
int em_level14_port_001B4CF0(const H14 *h, uint32_t p, F14 *fault);
int em_level14_port_001EDE40(const H14 *h, uint32_t a0, uint32_t a1, F14 *fault);
int em_level14_port_001F6BA0(const H14 *h, F14 *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL14_PORT_H */
