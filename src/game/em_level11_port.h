/* The eleventh level (AREA13's recharger [60], the roof walkway, the battery
 * machine [44] and [45]'s sequence, the cure g[3], the fallen boom and [7]'s
 * area, the region south of the pipe fence): standalone translations of the
 * functions the eleventh-level route census found new (decomp
 * build/s87/census/a13c_delta.json, new_functions: 43 rows;
 * ELEVENTH_LEVEL_ROUTE.md section 5) that had no verified port translation.
 * One row is reused, not translated here: 0020D930, whose table logic is
 * em_menu_hover_0020D930 (em_menu_hover.c), re-checked against the original
 * over the eleventh-level captures by the test.
 * Docs: docs/LEVEL11_PORT.md (the census rows, what each function does, the
 * verification and the binding).
 *
 * Ground truth: the original instructions. Every entry is compared with the
 * ORIGINAL code, run over recorded eleventh-level RAM images, by
 * tools/test_level11_port_reference.py; docs/LEVEL11_PORT.md section 3 says
 * exactly what that covers. Where the decomp's C text (NEARMISS rows) and
 * the original disagree, the original wins (the differences are listed in
 * the doc).
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
 * AREA13 overlay's own 0x824060 at its runtime address); pointer arguments
 * are original addresses; a hook returns >= 0 on success and its original
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
 * docs/LEVEL11_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL11_PORT_H
#define EM_LEVEL11_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL11_PORT_FAULT_NONE = 0,
    EM_LEVEL11_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL11_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL11_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_LEVEL11_PORT_FAULT_REGISTER = 7       /* a value the original leaves undefined, or an unmeasured VU0 form */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the function */
    int32_t code;     /* EM_LEVEL11_PORT_FAULT_* */
} EmLevel11PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- callees, by original address (written from the test's HOOKS table;
     * its header_checks compares this block with HOOKS on every run) ---- */
/* BEGIN GENERATED HOOKS */
    int (*w_001000E0)(void *ctx, uint64_t q0, uint64_t q1, int32_t *result);
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102850)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102900)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001157F0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_001179E0)(void *ctx, int32_t n0, uint32_t a1, int32_t *result);
    int (*w_00117BA0)(void *ctx, int32_t n0, int32_t n1, int32_t *result);
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E620)(void *ctx, float f0, float f1, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001274B0)(void *ctx, uint64_t q0, uint64_t q1, uint64_t *result);
    int (*w_00128250)(void *ctx, float f0, int32_t *result);
    int (*w_00128350)(void *ctx, float f0, uint64_t *result);
    int (*w_00131ED0)(void *ctx, uint32_t a0);
    int (*w_001434C0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00143610)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001437E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00143AF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00144040)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00144C20)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001450B0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00145850)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00146740)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_00146AF0)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_0019AD00)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019D330)(void *ctx, int32_t *result);
    int (*w_001A0B10)(void *ctx, int32_t *result);
    int (*w_001A6440)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001A7280)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001AFA90)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0D80)(void *ctx, uint32_t a0);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B13F0)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B1560)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1E20)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2B10)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001B2BF0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3, int32_t *result);
    int (*w_001B2F70)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B32F0)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B3390)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3, int32_t *result);
    int (*w_001B3440)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3, int32_t *result);
    int (*w_001B3580)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B37D0)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B39F0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001B4810)(void *ctx, uint32_t a0);
    int (*w_001B4CF0)(void *ctx, uint32_t a0);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C6120)(void *ctx, uint32_t a0, int32_t n1, uint32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001C7900)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3);
    int (*w_001CA1C0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001CA3B0)(void *ctx, uint32_t a0, float f1, float f2, float f3);
    int (*w_001CA4D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001CA7B0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001CA940)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001CB5F0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, uint32_t *result);
    int (*w_001CB900)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_001CB950)(void *ctx, uint32_t a0, int32_t n1, uint64_t q2, uint32_t *result);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CD370)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001CF470)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001CFA60)(void *ctx, uint32_t a0, uint32_t a1, float f2, float f3);
    int (*w_001CFB50)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4, float f5, float f6, float f7);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D04B0)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4);
    int (*w_001EFD20)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1, int32_t *result);
    int (*w_001F02C0)(void *ctx, uint32_t a0, int32_t n1, float f2);
    int (*w_001F3E30)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t n4);
    int (*w_001F8D30)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, float f4, float f5, float f6, uint32_t a7);
    int (*w_001FA790)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FAE70)(void *ctx, int32_t n0);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, int32_t *result);
    int (*w_0021C040)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00824060)(void *ctx, int32_t *result);
/* END GENERATED HOOKS */

    /* an indirect call through a function word read from memory */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t a0);
} EmLevel11PortHooks;

/* ---- AREA13 overlay (runtime addresses; OVERLAY/AREA13.BIN, id 10) ---- */
int em_level11_port_008240E0(const EmLevel11PortHooks *h, int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_00824390(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_00824520(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_008246D0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t self, uint32_t blk,
                             int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_008248C0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t self, uint32_t blk,
                             int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_00824960(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_008249F0(const EmLevel11PortHooks *h, uint32_t self, int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_00826610(const EmLevel11PortHooks *h, uint32_t src, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00827C30(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00827DD0(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_00827E00(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_00827F20(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_00827F90(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_008284E0(const EmLevel11PortHooks *h, uint32_t self, uint32_t other, EmLevel11PortFault *fault);
int em_level11_port_00828500(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00828C60(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00828E10(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_00828F40(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault);

/* ---- boot: the 0x141D20 actor family (e = the actor, d = e + 0x1F0) ---- */
int em_level11_port_00141D20(const EmLevel11PortHooks *h, uint32_t e, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00141F00(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault);
int em_level11_port_00142070(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00142330(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_001424C0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_001429D0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00145880(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault);
int em_level11_port_001459A0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault);
int em_level11_port_00146110(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, int32_t *result,
                             EmLevel11PortFault *fault);
int em_level11_port_001464B0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, int32_t *result,
                             EmLevel11PortFault *fault);
int em_level11_port_001469B0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_00146CE0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, int32_t *result,
                             EmLevel11PortFault *fault);
int em_level11_port_001471E0(const EmLevel11PortHooks *h, uint32_t e, uint32_t d, EmLevel11PortFault *fault);

/* ---- boot: the others ---- */
int em_level11_port_00100130(const EmLevel11PortHooks *h, uint64_t q0, uint64_t q1, int32_t *result,
                             EmLevel11PortFault *fault);
int em_level11_port_00118418(const EmLevel11PortHooks *h, uint32_t a0, int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_0019A6F0(const EmLevel11PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t a3,
                             int32_t t0, uint32_t sp, int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_001B2E50(const EmLevel11PortHooks *h, uint32_t a0, uint32_t out, int32_t *result,
                             EmLevel11PortFault *fault);
int em_level11_port_001B3F10(const EmLevel11PortHooks *h, uint32_t self, float bearing, float height, uint32_t sp,
                             int32_t *result, EmLevel11PortFault *fault);
int em_level11_port_001CDDC0(const EmLevel11PortHooks *h, int32_t layer, int32_t mode, uint32_t corners, uint64_t tex0,
                             uint32_t rgba, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_001CE660(const EmLevel11PortHooks *h, int32_t layer, int32_t tag, uint32_t mtx, uint32_t band,
                             uint64_t tex0, uint32_t rgba, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_001E4610(const EmLevel11PortHooks *h, uint32_t self, uint32_t sp, EmLevel11PortFault *fault);
int em_level11_port_001F4190(const EmLevel11PortHooks *h, uint32_t self, uint32_t timing, uint32_t cfg,
                             EmLevel11PortFault *fault);
int em_level11_port_001F4840(const EmLevel11PortHooks *h, uint32_t self, EmLevel11PortFault *fault);
int em_level11_port_001F9140(const EmLevel11PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                             float f12, uint32_t sp, EmLevel11PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL11_PORT_H */
