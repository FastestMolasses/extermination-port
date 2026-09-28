/* AREA01 revisit owners: standalone translations of the 17 AREA01 overlay
 * (OVERLAY/AREA01.BIN, header id 2) functions that only the AREA01 revisit
 * ran (decomp census build/s87/census/a02_delta.json, new_functions,
 * region overlay:AREA01; the revisit follows AREA00's progression exit).
 * Docs: docs/AREA01_REVISIT.md.
 *
 * Addresses are engine (runtime) addresses. The overlay is linked 0x40 below
 * where it runs, so the decomp's splat/link names are 0x40 lower (for
 * example 0x825950 is func_overlay_AREA01_00825910).
 *
 * Ground truth: the decomp's C under src/overlays/AREA01/, byte-identical
 * for all seventeen (docs/AREA01_OVERLAY_C.md in the decomp). Every entry is
 * compared with the ORIGINAL overlay code, run over the recorded revisit RAM
 * images, by tools/test_area01_revisit_reference.py: callee calls and
 * arguments, memory at every call entry (stack locals included), the memory
 * accesses between calls, final RAM, scratchpad and stack, and return
 * values, on the cases that test runs. docs/AREA01_REVISIT.md section 3 says
 * exactly what that covers; nothing beyond that is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory (actor records, globals, overlay data, scratchpad 0x70000000..,
 * and the original stack frame, below) goes through `bytes`, which maps an
 * original address range to native bytes laid out as the original
 * (little-endian, original offsets). Pointers stored in records are original
 * 32-bit addresses and are resolved through `bytes` again, exactly where
 * the original dereferences them.
 *
 * Stack locals: 0x8237D0, 0x823900, 0x825BE0 and 0x8282F0 keep vectors, a
 * matrix or a quad in their own stack frame, write them, and pass their
 * addresses to callees; 0x825950 reaches 0x825BE0. Those entries take `sp`,
 * the original stack pointer at entry, and address the locals at the
 * original frame offsets (sp - 0x60 .. sp for 0x8237D0, sp - 0x10 .. sp for
 * 0x823900, sp - 0x40 .. sp for 0x825BE0, sp - 0x20 .. sp for 0x8282F0;
 * 0x825950 runs 0x825BE0 with sp - 0x20). The module writes those locals
 * through `bytes` like any other memory and never reads them back; the
 * callees read them. The caller must map that frame region.
 *
 * Callees: every boot function, the AREA01 overlay function 0x826010 (not in
 * this set) and the actor's +0x4C callback are hooks named by original
 * address. Pointer arguments are original addresses. A hook returns >= 0 on
 * success; its original result (when the original has one) is written to
 * *result. The module never simulates a callee. The seventeen call each
 * other directly (0x825950 -> 0x825BE0 / 0x825D30 / 0x825EA0 / 0x825F00 /
 * 0x825FC0, 0x824340 -> 0x824F70 / 0x824FE0 / 0x825040). `ctx` is passed
 * unchanged to `bytes`, every hook and the callback, and never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * an entry with a result does not write *result, and the entry returns -1.
 * A fault already latched on entry makes the call return -1 without doing
 * anything; so do a NULL hook table, a NULL fault pointer and (for the
 * entries with a result) a NULL result pointer. A hook that re-enters an
 * entry with the same fault record is not covered.
 *
 * Unbound: nothing in the port calls these entries yet (level-3/4 side
 * track; docs/AREA01_REVISIT.md "Binding").
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA01_REVISIT_H
#define EM_AREA01_REVISIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry addresses. The callers are read from the decomp C; the data words
 * naming the behaviours were found in the revisit RAM images; the group and
 * script names follow docs/FOURTH_LEVEL_ROUTE.md section 2.1. */
#define EM_AREA01_RV_8237D0 0x008237D0u /* behaviour word at 0x82AA04 (group 0x82A900) */
#define EM_AREA01_RV_823900 0x00823900u /* behaviour word at 0x82AA30 (group 0x82A900) */
#define EM_AREA01_RV_8239C0 0x008239C0u /* behaviour word at 0x82AA5C (group 0x82A900) */
#define EM_AREA01_RV_824340 0x00824340u /* called by 0x823CD0 (+0x05 2..5) */
#define EM_AREA01_RV_824770 0x00824770u /* called by 0x823CD0 (+0x05 6) */
#define EM_AREA01_RV_824D50 0x00824D50u /* called by 0x823CD0 (+0x05 7) */
#define EM_AREA01_RV_824F70 0x00824F70u /* called by 0x824340 */
#define EM_AREA01_RV_824FE0 0x00824FE0u /* called by 0x824340 */
#define EM_AREA01_RV_825040 0x00825040u /* called by 0x824340 */
#define EM_AREA01_RV_825910 0x00825910u /* op09 callback named by record 0x82ACD0 (script 0x82AC10) */
#define EM_AREA01_RV_825950 0x00825950u /* behaviour of group 0x828A00 records [5] and [7] */
#define EM_AREA01_RV_825BE0 0x00825BE0u /* called by 0x825950 (D_008107DF 0, 1) */
#define EM_AREA01_RV_825D30 0x00825D30u /* called by 0x825950 (D_008107DF 2, 0x10) */
#define EM_AREA01_RV_825EA0 0x00825EA0u /* called by 0x825950 (D_008107DF 0x40) */
#define EM_AREA01_RV_825F00 0x00825F00u /* called by 0x825950 (D_008107DF 0x80) */
#define EM_AREA01_RV_825FC0 0x00825FC0u /* called by 0x825950 (+0x0D 0x4B, D_008107DF 0xFF) */
#define EM_AREA01_RV_8282F0 0x008282F0u /* called by 0x826D40 (em_area01_overlay's w_008282F0) */

enum {
    EM_AREA01_RV_FAULT_NONE = 0,
    EM_AREA01_RV_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA01_RV_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA01_RV_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, or the unmapped data address */
    int32_t code;     /* EM_AREA01_RV_FAULT_* */
} EmArea01RevisitFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- callees, by original address -------------------------------- */
    int (*w_001026A0)(void *ctx, uint32_t dst, uint32_t matrix, uint32_t vector);
    int (*w_001026D0)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_001028B8)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_001028D0)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_00102738)(void *ctx, uint32_t a, uint32_t b, float *result);
    int (*w_00102760)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_00102948)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_00102958)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_001029C0)(void *ctx, uint32_t matrix);
    int (*w_00102A60)(void *ctx, uint32_t dst, uint32_t src, float angle);
    int (*w_00102B08)(void *ctx, uint32_t dst, uint32_t src, float angle);
    int (*w_00102BB0)(void *ctx, uint32_t dst, uint32_t src, float angle);
    int (*w_001031E0)(void *ctx, uint32_t dst, uint32_t src); /* copies three words */
    int (*w_00103230)(void *ctx, uint32_t dst, uint32_t src, float scale);
    int (*w_0011E2A8)(void *ctx, float x, float *result);
    int (*w_0011E748)(void *ctx, float x, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001287F0)(void *ctx, uint32_t actor, uint32_t block, int32_t clip, float f12);
    int (*w_00128830)(void *ctx, uint32_t actor, float f12, float f13, float f14);
    int (*w_0012D580)(void *ctx, uint32_t actor, uint32_t block, int32_t a2);
    int (*w_0012DE90)(void *ctx, uint32_t block);
    int (*w_0019A570)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t a3, int32_t *result);
    int (*w_0019AA80)(void *ctx, uint32_t a0, uint32_t a1, int32_t a2, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001AFA90)(void *ctx, int32_t cls, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t actor);
    int (*w_001B10B0)(void *ctx, uint32_t actor, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t origin, float x, float z, float *result);
    int (*w_001B12B0)(void *ctx, float goal, float current, float rate, float *result);
    int (*w_001B1470)(void *ctx, float angle, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t point, uint32_t quad, int32_t a3, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t record, uint32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t block, uint32_t script);
    int (*w_001BA1F0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001C2770)(void *ctx, uint32_t actor, uint32_t block, int32_t a2, int32_t *result);
    int (*w_001C3D60)(void *ctx, uint32_t actor, uint32_t block);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001C47A0)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001C63E0)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t actor, float dt, int32_t *result);
    int (*w_001C68C0)(void *ctx, uint32_t actor);
    int (*w_001C69A0)(void *ctx, uint32_t actor);
    int (*w_001CA6F0)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001CD520)(void *ctx, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result);
    int (*w_001E2BA0)(void *ctx, uint32_t start, uint32_t end, uint32_t colour, float length);
    int (*w_001EFD20)(void *ctx, int32_t id, uint32_t point);
    int (*w_001EFD90)(void *ctx, int32_t id, uint32_t a, uint32_t b);
    int (*w_001F4010)(void *ctx, int32_t a0, uint32_t matrix);
    int (*w_001F4E20)(void *ctx, uint32_t point, uint32_t colour, float f12);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001FB9F0)(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    int (*w_00826010)(void *ctx, uint32_t actor); /* AREA01 overlay function (not in this set) */

    /* ---- the actor's own callback: the function at actor +0x4C, called with the actor */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea01RevisitHooks;

/* Owners (pool behaviours) and the functions they call. Each is one call of
 * the original function; each returns 0, or -1 on a fault. Entries with
 * `sp` take the original stack pointer at entry (see "Stack locals"). */
int em_area01_revisit_008237D0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault);
int em_area01_revisit_00823900(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault);
int em_area01_revisit_008239C0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);
int em_area01_revisit_00824340(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault);
int em_area01_revisit_00824770(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault);
int em_area01_revisit_00824D50(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, EmArea01RevisitFault *fault);
int em_area01_revisit_00824FE0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);
int em_area01_revisit_00825950(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault);
int em_area01_revisit_00825BE0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t sp, EmArea01RevisitFault *fault);
int em_area01_revisit_00825D30(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);
int em_area01_revisit_00825EA0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);
int em_area01_revisit_00825F00(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);
int em_area01_revisit_00825FC0(const EmArea01RevisitHooks *h, uint32_t self, EmArea01RevisitFault *fault);

/* Entries with a result: *result receives the original return value; it is
 * left unwritten when the call returns -1. */
int em_area01_revisit_00824F70(const EmArea01RevisitHooks *h, uint32_t self, int32_t *result,
                               EmArea01RevisitFault *fault);
int em_area01_revisit_00825040(const EmArea01RevisitHooks *h, uint32_t self, uint32_t block, int32_t *result,
                               EmArea01RevisitFault *fault);
int em_area01_revisit_008282F0(const EmArea01RevisitHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                               int32_t *result, EmArea01RevisitFault *fault);

/* Script op09 callback (a0 = the owner whose +0x3C it reads, a1, a2): the
 * original reads only a0; the other two are taken so the entry has the op09
 * calling shape. *result: 1 while a0 +0x3C <= 505.0, else 0. */
int em_area01_revisit_00825910(const EmArea01RevisitHooks *h, uint32_t a0, uint32_t a1, uint32_t a2,
                               int32_t *result, EmArea01RevisitFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_REVISIT_H */
