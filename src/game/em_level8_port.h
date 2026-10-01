/* The eighth level (AREA06 -> AREA01 -> AREA22 -> AREA04 -> the lift ->
 * AREA13): standalone translations of the functions the a06b, a01v and a04b
 * route census found new (decomp build/s87/census/a06b_delta.json,
 * a01v_delta.json and a04b_delta.json, new_functions: 5, 3 and 45 rows;
 * a22b found none) that had no verified port translation. Docs:
 * docs/LEVEL8_PORT.md (the census rows, what is reused from other modules,
 * what each function does, the verification and the binding).
 *
 * Ground truth: the original instructions (read locally, never reproduced)
 * for every function, with the decomp's C as a guide where it is
 * byte-identical; where the decomp's C text and the instructions disagree
 * (NEARMISS, word and inline asm rows) the instructions win. Every entry is
 * compared with the ORIGINAL code, run over recorded eighth-level RAM
 * images, by tools/test_level8_port_reference.py; docs/LEVEL8_PORT.md
 * section 3 says exactly what that covers.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian). Pointers stored in
 * records are original 32-bit addresses, resolved through `bytes` again.
 * Entries whose original keeps locals in its stack frame take `sp`, the
 * original stack pointer at entry (16-byte aligned); the frame lives below
 * it at the original's offsets, and the caller maps [sp - 0x200, sp).
 *
 * Calls between the translations here are direct (no hook). Every other
 * boot function is a hook named by original address; pointer arguments
 * are original addresses; a hook returns >= 0 on success and its original
 * result (when used) in *result (float results as the float whose bits are
 * the original's). The actor's +0x4C method is `w_callback(ctx, function,
 * actor)`. `ctx` is passed unchanged and never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping (fault 5, also for a misaligned
 * address), a reached NULL hook (1), a negative hook result (2) or a float
 * form em_ee_float.h refuses (6) latches the first fault (address + code);
 * from then on no hook runs, `bytes` is not called, writes are dropped,
 * *result is not written and the entry returns -1. A fault latched on
 * entry, a NULL hook table, a NULL fault pointer and (entries with a
 * result) a NULL result pointer return -1 at once.
 *
 * Unbound: nothing in the port calls these entries (level side track;
 * docs/LEVEL8_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_LEVEL8_PORT_H
#define EM_LEVEL8_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_LEVEL8_PORT_FAULT_NONE = 0,
    EM_LEVEL8_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_LEVEL8_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_LEVEL8_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_LEVEL8_PORT_FAULT_FLOAT_FORM = 6,    /* em_ee_float.h refused a form */
    EM_LEVEL8_PORT_FAULT_REGISTER = 7       /* the original would use a caller's register here */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the refused form's site */
    int32_t code;     /* EM_LEVEL8_PORT_FAULT_* */
} EmLevel8PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees, by original address (generated from one table) -- */
/* BEGIN GENERATED HOOKS */
    int (*w_001000E0)(void *ctx, uint64_t q0, uint64_t q1, int32_t *result);
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001026D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102718)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102738)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102B08)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102C58)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E620)(void *ctx, float f0, float f1, float *result);
    int (*w_0011E748)(void *ctx, float f0, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_00128250)(void *ctx, float f0, int32_t *result);
    int (*w_00128350)(void *ctx, float f0, uint64_t *result);
    int (*w_00128640)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001287F0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, float f3);
    int (*w_0012F6C0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0012F980)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0012FA50)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001305B0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00130AB0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00131210)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00131510)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00131650)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00131E80)(void *ctx, uint32_t a0);
    int (*w_00131F20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001339E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00158590)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_00182F90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_0019A310)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AB20)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_0019AD00)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_0019BC40)(void *ctx, uint32_t a0);
    int (*w_001AEBA0)(void *ctx, int32_t n0);
    int (*w_001AF890)(void *ctx, uint32_t a0);
    int (*w_001AFA90)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0D80)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1020)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t n0);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B13F0)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B15D0)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0);
    int (*w_001B1D20)(void *ctx, uint32_t a0);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B2140)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B2E50)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B2F70)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001B3250)(void *ctx, uint32_t a0, uint32_t a1, float f2, int32_t *result);
    int (*w_001B3390)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, float f3, int32_t *result);
    int (*w_001B37D0)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B3F10)(void *ctx, uint32_t a0, float f1, float f2, int32_t *result);
    int (*w_001B4CF0)(void *ctx, uint32_t a0);
    int (*w_001B5360)(void *ctx, uint32_t a0);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001BC150)(void *ctx, uint32_t a0);
    int (*w_001BDCA0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BDD70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BF630)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001C1500)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3, float f4);
    int (*w_001C1570)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C2770)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_001C3D60)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001C6160)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001C62C0)(void *ctx, uint32_t a0);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63D0)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CFB50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, float f4, float f5, float f6, float f7);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D04B0)(void *ctx, uint32_t a0, int32_t n1, uint32_t a2, float f3, float f4);
    int (*w_001D0C80)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001D0D40)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3);
    int (*w_001D0D60)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001EFD20)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFD90)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2);
    int (*w_001EFE00)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFEB0)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFF10)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, float f6, uint32_t *result);
    int (*w_001EFFD0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, float f4);
    int (*w_001F4A00)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001F4E20)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_001F8D30)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, float f4, float f5, float f6, uint32_t a7);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, int32_t *result);
    int (*w_0021BB00)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_0021BED0)(void *ctx, uint32_t a0, int32_t *result);
    /* END GENERATED HOOKS */

    /* The actor's +0x4C method, called with the function read from +0x4C
     * and the actor. */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmLevel8PortHooks;

/* Entries without a result return 0, or -1 on a fault. Entries with a
 * result write the original return value (v0) to *result (not written on
 * -1). Arguments are the original's argument registers in order. */

/* ---- the lift [51] / [56], its buttons and the area-change segment ---- */
int em_level8_port_001BBD20(const EmLevel8PortHooks *h, uint32_t self, int32_t index, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001BC560(const EmLevel8PortHooks *h, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001BC6D0(const EmLevel8PortHooks *h, uint32_t self, uint32_t st, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001BC740(const EmLevel8PortHooks *h, uint32_t self, uint32_t blk, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001BC860(const EmLevel8PortHooks *h, uint32_t self, uint32_t blk, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001BD180(const EmLevel8PortHooks *h, uint32_t rec, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001BD270(const EmLevel8PortHooks *h, uint32_t rec, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001BD9F0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_001BDE60(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);

/* ---- AREA01's gap node (the a06b_01 load, the a01v jump) ---- */
int em_level8_port_001284E0(const EmLevel8PortHooks *h, uint32_t owner, uint32_t pos, int32_t kind, uint32_t dir,
                            int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_0012B850(const EmLevel8PortHooks *h, uint32_t self, uint32_t sub, EmLevel8PortFault *fault);
int em_level8_port_001BE6C0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_001BEAC0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, int32_t a2, int32_t a3,
                            int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001BF5B0(const EmLevel8PortHooks *h, uint32_t a0, uint32_t p, int32_t mode,
                            EmLevel8PortFault *fault);
int em_level8_port_001BF6B0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_001BFF90(const EmLevel8PortHooks *h, uint32_t obj, uint32_t p, int32_t clip,
                            EmLevel8PortFault *fault);

/* ---- the creature at AREA04's lift (behaviour 0012E3A0; ent = self +0x1F0) ---- */
int em_level8_port_0012E3A0(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, EmLevel8PortFault *fault);
int em_level8_port_0012E560(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, EmLevel8PortFault *fault);
int em_level8_port_0012E840(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_0012EB60(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_0012F100(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_0012FC10(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_00131ED0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_00131F90(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_00132490(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_001328D0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp,
                            EmLevel8PortFault *fault);
int em_level8_port_00132FB0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, uint32_t sp, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001333F0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_00133640(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_00133A20(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_00133DB0(const EmLevel8PortHooks *h, uint32_t self, uint32_t ent, EmLevel8PortFault *fault);
/* 0011E0A8 (modff): *result is the fraction (f0); the integral part goes to ptr. */
int em_level8_port_0011E0A8(const EmLevel8PortHooks *h, uint32_t ptr, float x, float *result,
                            EmLevel8PortFault *fault);
int em_level8_port_0021BE40(const EmLevel8PortHooks *h, uint32_t self, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001B4810(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);

/* ---- the creature's probes ---- */
int em_level8_port_001A7B80(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001A7BA0(const EmLevel8PortHooks *h, uint32_t a, uint32_t b, uint32_t m1, uint32_t m2,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001B1560(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float limit, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001B2B10(const EmLevel8PortHooks *h, uint32_t self, uint32_t dst, uint32_t src,
                            EmLevel8PortFault *fault);
int em_level8_port_001B2BF0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, uint32_t out, float limit,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001B30E0(const EmLevel8PortHooks *h, uint32_t pos, uint32_t out, int32_t *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001B32F0(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float limit, uint32_t sp,
                            int32_t *result, EmLevel8PortFault *fault);
int em_level8_port_001B3440(const EmLevel8PortHooks *h, uint32_t self, uint32_t a, uint32_t b, float limit,
                            uint32_t sp, int32_t *result, EmLevel8PortFault *fault);
/* 001B3580: *result is the heading (f0). */
int em_level8_port_001B3580(const EmLevel8PortHooks *h, uint32_t self, uint32_t pos, float *result,
                            EmLevel8PortFault *fault);
int em_level8_port_001B55E0(const EmLevel8PortHooks *h, uint32_t self, int32_t kind, EmLevel8PortFault *fault);

/* ---- packets and effects ---- */
int em_level8_port_001EAD70(const EmLevel8PortHooks *h, int32_t a0, int32_t a1, EmLevel8PortFault *fault);
int em_level8_port_001ED100(const EmLevel8PortHooks *h, int32_t a0, int32_t a1, EmLevel8PortFault *fault);
int em_level8_port_001F91C0(const EmLevel8PortHooks *h, uint32_t self, uint32_t sp, EmLevel8PortFault *fault);

/* ---- AREA04 overlay (runtime addresses; OVERLAY/AREA04.BIN, id 5) ---- */
int em_level8_port_00823920(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_008239A0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_00823A90(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);
int em_level8_port_008245F0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_LEVEL8_PORT_H */
