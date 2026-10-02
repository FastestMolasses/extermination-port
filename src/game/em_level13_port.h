/* The thirteenth level ([7]'s room, door [27] and AREA19 sub 1: the a19c
 * route): standalone translations of the functions the thirteenth-level
 * route census found new (decomp build/s87/census/a19c_delta.json,
 * new_functions: 36 rows; THIRTEENTH_LEVEL_ROUTE.md section 5.2) that had no
 * verified port translation (31 of them; the other 5 are the verified
 * em_player_closure_10_12_19 translations, reused). Docs: docs/LEVEL13_PORT.md
 * (the census rows, what each function does, the verification and the
 * binding).
 *
 * Ground truth: the original instructions. Every entry is compared with the
 * ORIGINAL code, run over recorded thirteenth-level RAM images, by
 * tools/test_level13_port_reference.py; docs/LEVEL13_PORT.md section 3 says
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
 * the original's; 64-bit arguments whole). The indirect call through a
 * function word read from memory (the actor's +0x4C method) is
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
 * docs/LEVEL13_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL13_PORT_H
#define EM_LEVEL13_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL13_PORT_FAULT_NONE = 0,
    EM_LEVEL13_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL13_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL13_PORT_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL13_PORT_FAULT_* */
} EmLevel13PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- callees, by original address (written from the test's HOOKS table;
     * its header_checks compares this block with HOOKS on every run) ---- */
/* BEGIN GENERATED HOOKS */
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00103230)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_0013BA20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013BBB0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0013C8C0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001916C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AD00)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001A7B80)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001AFA90)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B1270)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1E20)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B2D00)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B2F70)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t a0);
    int (*w_001B6F00)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C4760)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C47A0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C47E0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C7420)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_001CA770)(void *ctx, uint32_t a0);
    int (*w_001CAAC0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CFA60)(void *ctx, uint32_t a0, uint32_t a1, float f2, float f3);
    int (*w_001CFB50)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4, float f5, float f6, float f7);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D1F80)(void *ctx, int32_t n0, int32_t n1, int32_t n2);
    int (*w_001D1FF0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001D2090)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001D2910)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001D6F60)(void *ctx, int32_t n0, uint64_t q1, int32_t n2);
    int (*w_001D8C20)(void *ctx, int32_t n0);
    int (*w_001EFD90)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2);
    int (*w_001EFEB0)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001F02C0)(void *ctx, uint32_t a0, int32_t n1, float f2);
    int (*w_001F0460)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001F1110)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001F1180)(void *ctx, uint32_t a0);
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F5940)(void *ctx, int32_t n0, uint32_t a1, int32_t n2);
    int (*w_001F6640)(void *ctx, uint32_t a0);
    int (*w_001F6BA0)(void *ctx);
    int (*w_001FB0B0)(void *ctx, int32_t n0);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_001FC3C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, float f3, float f4);
    int (*w_001FC520)(void *ctx, uint32_t a0);
    int (*w_0021B9A0)(void *ctx, int32_t n0, float f1, float f2);
    int (*w_0021BE40)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0021BED0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BF90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0021C040)(void *ctx, uint32_t a0, uint32_t a1);
/* END GENERATED HOOKS */

    /* the indirect call through a function word (the actor's +0x4C method) */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel13PortHooks;

typedef EmLevel13PortHooks H13;
typedef EmLevel13PortFault F13;

/* ---- AREA19 overlay (runtime addresses; self = the pool node) ---- */
int em_level13_port_00823780(const H13 *h, uint32_t self, uint32_t sp, F13 *fault);
int em_level13_port_00823D10(const H13 *h, uint32_t self, uint32_t sp, F13 *fault);
int em_level13_port_00824A90(const H13 *h, uint32_t self, uint32_t sp, F13 *fault);
int em_level13_port_00826570(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00826840(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00826C10(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00827430(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00827550(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_008279E0(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00827B10(const H13 *h, int32_t *result, F13 *fault);
int em_level13_port_00827B20(const H13 *h, int32_t *result, F13 *fault);
int em_level13_port_00827B60(const H13 *h, uint32_t self, F13 *fault);
int em_level13_port_00829840(const H13 *h, uint32_t src, uint32_t sp, F13 *fault);
int em_level13_port_00829A70(const H13 *h, uint32_t self, F13 *fault);

/* ---- boot: 001386E0's behaviours 2 / 3 / 4 (ent = self + 0x1F0) and the
 * 001549C0 tendrils ---- */
int em_level13_port_00139240(const H13 *h, uint32_t self, uint32_t ent, F13 *fault);
int em_level13_port_00139E00(const H13 *h, uint32_t self, uint32_t ent, F13 *fault);
int em_level13_port_0013A3B0(const H13 *h, uint32_t self, uint32_t ent, F13 *fault);
int em_level13_port_001545B0(const H13 *h, uint32_t self, float x, float z, int32_t *result, F13 *fault);
int em_level13_port_00154F00(const H13 *h, uint32_t self, uint32_t sp, F13 *fault);

/* ---- boot: the others ---- */
int em_level13_port_00194240(const H13 *h, uint32_t p, int32_t *result, F13 *fault);
int em_level13_port_00197390(const H13 *h, uint32_t cam, uint32_t other, F13 *fault);
int em_level13_port_001B2B80(const H13 *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, F13 *fault);
int em_level13_port_001B3250(const H13 *h, uint32_t self, uint32_t point, float lim, uint32_t sp, int32_t *result,
                             F13 *fault);
int em_level13_port_001B34F0(const H13 *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, F13 *fault);
int em_level13_port_001B37D0(const H13 *h, uint32_t self, float dist, float lim, uint32_t sp, float *result,
                             F13 *fault);
int em_level13_port_001CB140(const H13 *h, uint32_t a0, uint32_t a1, F13 *fault);
int em_level13_port_001CB1F0(const H13 *h, uint32_t obj, F13 *fault);
int em_level13_port_001D42E0(const H13 *h, int32_t ch, uint32_t model, F13 *fault);
int em_level13_port_001D4430(const H13 *h, uint32_t model, uint32_t a1, F13 *fault);
int em_level13_port_001F6AC0(const H13 *h, uint32_t rec, int32_t *result, F13 *fault);
int em_level13_port_001F6B90(const H13 *h, F13 *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL13_PORT_H */
