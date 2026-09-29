/* AREA01 upper floor and AREA06 (level 7): standalone translations of the
 * functions the a01u and a06 route census found new (decomp
 * build/s87/census/a01u_delta.json and a06_delta.json, new_functions: 7 and
 * 32 rows) that had no verified port translation. Docs:
 * docs/AREA06_PORT.md (the census rows, what is reused from other modules,
 * what each function does, the verification and the binding).
 *
 * Translated here:
 *   boot     00123020 (the EE string compare), 00169250 (player +5 state:
 *            the overhead bar's approach), 00176180, 001885B0, 001944B0
 *            (camera states 29..37), 0019F680, 001A8F40, 001EA210,
 *            001EBE10, 001ECA20, 002072A0, 002072C0, 00207350 (the keypad
 *            page) with 002079F0 / 00207BB0 / 00207CA0 / 00207CD0,
 *            00219870 (the node behaviour of AREA06's deferred g[11]) with
 *            00219F50 / 0021A180 / 0021A440, 0021AE90, 001CE860 (the
 *            strip packet builder of the 0x8000003B strip nodes 0021A500)
 *   AREA06   0x823580 (placement [9]), 0x8242C0 (a script op09
 *            callback), 0x824560 (the beam [11]); runtime addresses
 *            (OVERLAY/AREA06.BIN, id 6, splat/link names 0x40 lower)
 *   AREA01   0x826950 (the op09 callback of script 0x82B590, the catwalk
 *            event; OVERLAY/AREA01.BIN, id 2)
 * The other census rows have translations in other modules and are reused
 * (docs/AREA06_PORT.md section 0).
 *
 * Ground truth: the decomp's C where it is byte-identical, and the original
 * instructions (read locally, never reproduced) for the NEARMISS, inline
 * asm and word asm rows and for the load / store order and float operand
 * order of every function. Every entry is compared with the ORIGINAL code,
 * run over recorded a01u / a06 RAM images, by
 * tools/test_area06_port_reference.py; docs/AREA06_PORT.md section 3 says
 * exactly what that covers.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory goes through `bytes`, which maps an original address range to
 * native bytes laid out as the original (little-endian). Pointers stored in
 * records are original 32-bit addresses, resolved through `bytes` again.
 * Entries whose original keeps locals in its stack frame take `sp`, the
 * original stack pointer at entry (16-byte aligned); the frame lives below
 * it at the original's offsets, and the caller maps [sp - 0x100, sp).
 *
 * Calls between the translations here are direct (no hook). Every other
 * boot function is a hook named by original address; pointer arguments
 * are original addresses; a hook returns >= 0 on success and its original
 * result (when used) in *result (float results as the float whose bits are
 * the original's; 64-bit register arguments as uint64_t). The actor's +0x4C
 * method (00219870, 0x824560) is `w_callback(ctx, function, actor)`. `ctx`
 * is passed unchanged and never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping (fault 5, also for a misaligned
 * address), a reached NULL hook (1), a negative hook result (2) or a VU0 /
 * float form em_ee_float.h refuses (6) latches the first fault (address +
 * code); from then on no hook runs, `bytes` is not called, writes are
 * dropped, *result is not written and the entry returns -1. A fault
 * latched on entry, a NULL hook table, a NULL fault pointer and (entries
 * with a result) a NULL result pointer return -1 at once.
 *
 * Unbound: nothing in the port calls these entries (level side track;
 * docs/AREA06_PORT.md "Binding"). stdint only; EE float model.
 */
#ifndef EM_AREA06_PORT_H
#define EM_AREA06_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EM_AREA06_PORT_FAULT_NONE = 0,
    EM_AREA06_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA06_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA06_PORT_FAULT_BAD_ADDRESS = 5,   /* `bytes` could not map an address */
    EM_AREA06_PORT_FAULT_FLOAT_FORM = 6     /* em_ee_float.h refused a form */
};

typedef struct {
    uint32_t address; /* original callee, the unmapped data address, or the refused form's site */
    int32_t code;     /* EM_AREA06_PORT_FAULT_* */
} EmArea06PortFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees, by original address -------------------------- */
    int (*w_0011DE90)(void *ctx, float f0, float *result);
    int (*w_0011DF78)(void *ctx, float f0, float *result);
    int (*w_0011E2A8)(void *ctx, float f0, float *result);
    int (*w_0011E748)(void *ctx, float f0, float *result);
    int (*w_001026A0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102738)(void *ctx, uint32_t a0, uint32_t a1, float *result);
    int (*w_00102760)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001028B8)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102900)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102918)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00102958)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001029C0)(void *ctx, uint32_t a0);
    int (*w_00102BB0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float f0, int32_t *result);
    int (*w_001749A0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_00182F90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_0018C4B0)(void *ctx, uint32_t a0, float f1, float f2);
    int (*w_0018C6A0)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_0018D7B0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_00191120)(void *ctx, int32_t n0, float f1, float f2, float f3, float f4, float *result);
    int (*w_00194240)(void *ctx, uint32_t a0);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_0019AFE0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AED80)(void *ctx, int32_t n0);
    int (*w_001AEDB0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2);
    int (*w_001AF800)(void *ctx, uint32_t a0);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001AFF10)(void *ctx, uint32_t *result);
    int (*w_001AFF90)(void *ctx, uint32_t a0);
    int (*w_001B0000)(void *ctx);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t n0);
    int (*w_001B11E0)(void *ctx, int32_t n0, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B12B0)(void *ctx, float f0, float f1, float f2, float *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B1630)(void *ctx, float f0, float f1, float f2, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0);
    int (*w_001C5570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001CB5B0)(void *ctx, int32_t n0);
    int (*w_001CB5F0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, uint32_t *result);
    int (*w_001CB900)(void *ctx, uint32_t a0, int32_t n1, int32_t n2);
    int (*w_001CC1E0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, uint32_t a5, int32_t n6);
    int (*w_001CCF70)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001CD370)(void *ctx, int32_t n0, uint32_t *result);
    int (*w_001CD390)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001CFA60)(void *ctx, uint32_t a0, uint32_t a1, float f2, float f3);
    int (*w_001CFB50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3, float f4, float f5, float f6, float f7);
    int (*w_001CFBE0)(void *ctx, int32_t n0, int32_t n1, uint32_t a2, uint32_t a3, int32_t n4);
    int (*w_001D2DE0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001EFD20)(void *ctx, int32_t n0, uint32_t a1);
    int (*w_001EFEB0)(void *ctx, int32_t n0, uint32_t a1, uint32_t *result);
    int (*w_001F02C0)(void *ctx, uint32_t a0, int32_t n1, float f2);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FB0B0)(void *ctx, int32_t n0);
    int (*w_001FB9F0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);
    int (*w_00207D00)(void *ctx, int32_t n0, int32_t n1);
    int (*w_00207E40)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3, int32_t n4, uint32_t a5, uint64_t q6);
    int (*w_0020CD60)(void *ctx);

    /* The actor's +0x4C method, called by 00219870 and 0x824560 with the
     * function read from +0x4C and the actor. */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea06PortHooks;

/* Entries without a result return 0, or -1 on a fault. Entries with a
 * result write the original return value to *result (not written on -1). */
int em_area06_port_00123020(const EmArea06PortHooks *h, uint32_t a, uint32_t b, int32_t *result,
                            EmArea06PortFault *fault);
int em_area06_port_00169250(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault);
int em_area06_port_00176180(const EmArea06PortHooks *h, uint32_t actor, uint32_t a1, uint32_t target, uint32_t sp,
                            EmArea06PortFault *fault);
int em_area06_port_001885B0(const EmArea06PortHooks *h, uint32_t actor, int32_t *result, EmArea06PortFault *fault);
int em_area06_port_001944B0(const EmArea06PortHooks *h, uint32_t cam, uint32_t ent, int32_t idx, int32_t *result,
                            EmArea06PortFault *fault);
int em_area06_port_0019F680(const EmArea06PortHooks *h, uint32_t out, uint32_t rec, int32_t index, int32_t *result,
                            EmArea06PortFault *fault);
int em_area06_port_001A8F40(const EmArea06PortHooks *h, uint32_t a, uint32_t b, EmArea06PortFault *fault);
int em_area06_port_001EA210(const EmArea06PortHooks *h, float value, EmArea06PortFault *fault);
int em_area06_port_001EBE10(const EmArea06PortHooks *h, uint32_t a0, uint32_t a1, EmArea06PortFault *fault);
int em_area06_port_001ECA20(const EmArea06PortHooks *h, uint32_t a0, uint32_t a1, EmArea06PortFault *fault);
int em_area06_port_002072A0(const EmArea06PortHooks *h, EmArea06PortFault *fault);
int em_area06_port_002072C0(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault);
int em_area06_port_00207350(const EmArea06PortHooks *h, uint32_t page, uint32_t sp, EmArea06PortFault *fault);
int em_area06_port_002079F0(const EmArea06PortHooks *h, uint32_t page, uint32_t tex, EmArea06PortFault *fault);
int em_area06_port_00207BB0(const EmArea06PortHooks *h, uint32_t page, uint32_t tex, uint32_t sp,
                            EmArea06PortFault *fault);
int em_area06_port_00207CA0(const EmArea06PortHooks *h, uint32_t tex, EmArea06PortFault *fault);
int em_area06_port_00207CD0(const EmArea06PortHooks *h, uint32_t tex, EmArea06PortFault *fault);
int em_area06_port_00219870(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, EmArea06PortFault *fault);
int em_area06_port_00219F50(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault);
int em_area06_port_0021A180(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                            EmArea06PortFault *fault);
int em_area06_port_0021A440(const EmArea06PortHooks *h, uint32_t self, uint32_t src, EmArea06PortFault *fault);
int em_area06_port_0021AE90(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault);
/* 001CE860 (the strip packet): a0 / a1 as the original's words, the point
 * array (16 bytes per point), the colour block, the point count, the 64-bit
 * GIF tag word, the half width, and `sp`. */
int em_area06_port_001CE860(const EmArea06PortHooks *h, int32_t a0, int32_t a1, uint32_t points, uint32_t colour,
                            int32_t count, uint64_t tag, float width, uint32_t sp, EmArea06PortFault *fault);

/* AREA06 overlay (runtime addresses). */
int em_area06_port_00823580(const EmArea06PortHooks *h, uint32_t self, EmArea06PortFault *fault);
int em_area06_port_008242C0(const EmArea06PortHooks *h, uint32_t self, uint32_t st, int32_t *result,
                            EmArea06PortFault *fault);
int em_area06_port_00824560(const EmArea06PortHooks *h, uint32_t self, uint32_t sp, EmArea06PortFault *fault);

/* AREA01 overlay (runtime address). */
int em_area06_port_00826950(const EmArea06PortHooks *h, uint32_t self, uint32_t st, uint32_t prm, int32_t *result,
                            EmArea06PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA06_PORT_H */
