/* AREA02 overlay owners: standalone translations of the 15 AREA02 overlay
 * (OVERLAY/AREA02.BIN, header id 3) functions that ran on the recorded
 * AREA02 route (decomp census build/s87/census/a02_delta.json,
 * new_functions, region overlay:AREA02), plus 0x825520, the one function
 * they reach that the route did not run (0x825100 calls it when D_0081083F
 * is 2). Docs: docs/AREA02_OVERLAY_PORT.md.
 *
 * Addresses are engine (runtime) addresses. The overlay is linked 0x40 below
 * where it runs, so the decomp's splat/link names are 0x40 lower (for
 * example 0x8242F0 is func_overlay_AREA02_008242B0).
 *
 * Ground truth: the decomp's C under src/overlays/AREA02/, byte-identical
 * for all sixteen (docs/AREA02_OVERLAY.md in the decomp; 008254E0 links
 * from its splat pieces for a link-tool reason, its object is identical).
 * Every function is compared with the ORIGINAL overlay code, run over the
 * recorded AREA02 RAM images, by tools/test_area02_overlay_reference.py:
 * callee calls and arguments, memory at every call entry, the memory
 * accesses between calls, final RAM, scratchpad and stack locals, and
 * return values, on the cases that test runs.
 * docs/AREA02_OVERLAY_PORT.md section 3 says exactly what that covers;
 * nothing beyond that is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory (actor records, globals, overlay data, scratchpad 0x70000000..,
 * and the stack locals below) goes through `bytes`, which maps an original
 * address range to native bytes laid out as the original (little-endian,
 * original offsets). Pointers stored in records are original 32-bit
 * addresses and are resolved through `bytes` again, exactly where the
 * original dereferences them.
 *
 * Calls inside the overlay: 0x823930 dispatches to 0x823980 / 0x824020,
 * 0x823980 calls 0x824D50, 0x824020 calls 0x824800 / 0x8242F0, 0x8242F0
 * calls 0x824CD0 / 0x824910 / 0x824AC0, 0x824CD0 calls 0x824C40 and
 * 0x825100 calls 0x825520. The translations call each other the same way
 * (no hook); every entry is also public.
 *
 * Stack locals: 0x824910 (two 16-byte vectors) and 0x824CD0 (four) keep
 * locals in their own stack frame, write them, and pass their addresses to
 * callees. The entries that reach them (0x823930, 0x824020, 0x8242F0,
 * 0x824910, 0x824CD0) take `sp`, the original stack pointer at entry; each
 * nested call gets sp minus the caller's original frame size, and a local
 * is at its original frame offset. The module reads and writes those
 * locals through `bytes` like any other memory, so the caller must map
 * the frames below sp for the module and the callees (the locals lie in
 * [sp - 0xB0, sp - 0x70) for the 0x823930 entry, the deepest chain; the
 * callees also use their own frames further down).
 *
 * Callees: every boot function and the actor's +0x4C callback are hooks
 * named by original address. Pointer arguments are original addresses. A
 * hook returns >= 0 on success; its original result (when the original uses
 * one) is written to *result (float results as the float whose bits are the
 * original's). The module never simulates a callee. `ctx` is passed
 * unchanged to `bytes`, every hook and the callback, and never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * an entry with a result does not write *result, and the entry returns -1.
 * A fault already latched on entry makes the call return -1 without doing
 * anything; so do a NULL hook table, a NULL fault pointer and (for the
 * entries with a result) a NULL result pointer. Which of these the test
 * checks, and how, is docs/AREA02_OVERLAY_PORT.md section 3; a hook that
 * re-enters an entry with the same fault record is not covered.
 *
 * Unbound: nothing in the port calls these entries yet (level-3/4 side
 * track; docs/AREA02_OVERLAY_PORT.md "Binding").
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA02_OVERLAY_H
#define EM_AREA02_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry addresses. Behaviour words at actor +0x10 on the recorded route
 * (docs/FOURTH_LEVEL_ROUTE.md sections 1.1 and 2.4 name the nodes; the
 * code does not): 0x823930 (placements [31]..[35]), 0x823D70 ([36]..[38]
 * and one more kind-14 node), 0x824FA0 (one examine owner), 0x825100 ([27],
 * [28]) and 0x823580 (a spawned node). 0x823900 is the area's
 * init. The others are called from inside the overlay. */
#define EM_AREA02_OVL_823580 0x00823580u /* owner, +0x1F0 block, effect spawns */
#define EM_AREA02_OVL_INIT   0x00823900u /* area init: five boot words */
#define EM_AREA02_OVL_823930 0x00823930u /* dispatch by +0x02 & 0x1F: 9 -> 0x823980, 4 -> 0x824020 */
#define EM_AREA02_OVL_823980 0x00823980u /* the switch owner (kind 9) */
#define EM_AREA02_OVL_823D70 0x00823D70u /* the gates, by +0x0D (14, 15, 16) */
#define EM_AREA02_OVL_824020 0x00824020u /* kind-4 owners by +0x0D (7, 8, 10, 11) */
#define EM_AREA02_OVL_8242F0 0x008242F0u /* +0x0D 7 / 8 run step; returns 1 when 7 ends */
#define EM_AREA02_OVL_824800 0x00824800u /* model setup; returns 0 or 1 */
#define EM_AREA02_OVL_824910 0x00824910u /* the 18 trigger records at 0x827350 */
#define EM_AREA02_OVL_824AC0 0x00824AC0u /* four 0x80000030 spawns every 8th frame */
#define EM_AREA02_OVL_824C40 0x00824C40u /* one grey sprite at a point */
#define EM_AREA02_OVL_824CD0 0x00824CD0u /* two sprites at bone-transformed points */
#define EM_AREA02_OVL_824D50 0x00824D50u /* player-in-zone test; returns 0, 1 or 2 */
#define EM_AREA02_OVL_824FA0 0x00824FA0u /* examine owner (+0x30 = 0x2758E0, script 0x827670) */
#define EM_AREA02_OVL_825100 0x00825100u /* by +0x03: 7 (turning object) or 4 (a Use script) */
#define EM_AREA02_OVL_825520 0x00825520u /* turns the player about a +0x0D 9 object */

enum {
    EM_AREA02_OVL_FAULT_NONE = 0,
    EM_AREA02_OVL_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA02_OVL_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA02_OVL_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, or the unmapped data address */
    int32_t code;     /* EM_AREA02_OVL_FAULT_* */
} EmArea02OvlFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees, by original address -------------------------- */
    int (*w_0011DE90)(void *ctx, float x, float *result);
    int (*w_0011E2A8)(void *ctx, float x, float *result);
    int (*w_001026A0)(void *ctx, uint32_t dst, uint32_t matrix, uint32_t vector);
    int (*w_00121A28)(void *ctx, uint32_t dst, int32_t value, int32_t count);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_0019C6F0)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001A2370)(void *ctx, uint32_t actor, uint32_t matrix);
    int (*w_001AA700)(void *ctx, uint32_t actor);
    int (*w_001AF780)(void *ctx, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t actor);
    int (*w_001B0FD0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B12B0)(void *ctx, float goal, float current, float rate, float *result);
    int (*w_001B1470)(void *ctx, float angle, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t actor);
    int (*w_001B18F0)(void *ctx, uint32_t actor, uint32_t a, uint32_t b, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t actor);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t point, uint32_t quad, int32_t count, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t record);
    int (*w_001B6F00)(void *ctx, uint32_t actor, uint32_t point, float yaw);
    int (*w_001BA1A0)(void *ctx, uint32_t block, uint32_t script);
    int (*w_001BA1C0)(void *ctx, uint32_t actor, int32_t index, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001C6120)(void *ctx, uint32_t table, int32_t id, uint32_t *result);
    int (*w_001C6150)(void *ctx, uint32_t model, int32_t *result);
    int (*w_001C6380)(void *ctx, uint32_t actor);
    int (*w_001C63E0)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t actor, float dt);
    int (*w_001C68C0)(void *ctx, uint32_t actor);
    int (*w_001CA6E0)(void *ctx, uint32_t actor, uint32_t model);
    int (*w_001CB5B0)(void *ctx, int32_t a0);
    int (*w_001CD520)(void *ctx, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, float w, float h, float zbias, uint32_t rgba);
    int (*w_001EFD20)(void *ctx, int32_t id, uint32_t point, uint32_t *result);
    int (*w_001EFD90)(void *ctx, int32_t id, uint32_t a, uint32_t b, int32_t *result);
    int (*w_001F02C0)(void *ctx, uint32_t point, int32_t id, float volume);
    int (*w_001F4BF0)(void *ctx, uint32_t a, uint32_t b);
    int (*w_001F6B30)(void *ctx);
    int (*w_001FA790)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FAE70)(void *ctx, int32_t a0);

    /* ---- the actor's own callback: the function at actor +0x4C, called with the actor */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea02OvlHooks;

/* Every entry returns 0, or -1 on a fault. `sp` is the original stack
 * pointer at entry (see "Stack locals" above). */
int em_area02_ovl_00823580(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00823900(const EmArea02OvlHooks *h, EmArea02OvlFault *fault);
int em_area02_ovl_00823930(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault);
int em_area02_ovl_00823980(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00823D70(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00824020(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault);
int em_area02_ovl_00824910(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault);
int em_area02_ovl_00824AC0(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00824C40(const EmArea02OvlHooks *h, uint32_t point, EmArea02OvlFault *fault);
int em_area02_ovl_00824CD0(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, EmArea02OvlFault *fault);
int em_area02_ovl_00824FA0(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00825100(const EmArea02OvlHooks *h, uint32_t self, EmArea02OvlFault *fault);
int em_area02_ovl_00825520(const EmArea02OvlHooks *h, EmArea02OvlFault *fault);

/* Entries with a result: *result receives the original return value; it
 * is left unwritten when the call returns -1. */
int em_area02_ovl_008242F0(const EmArea02OvlHooks *h, uint32_t self, uint32_t sp, int32_t *result,
                           EmArea02OvlFault *fault);
int em_area02_ovl_00824800(const EmArea02OvlHooks *h, uint32_t self, int32_t *result, EmArea02OvlFault *fault);
int em_area02_ovl_00824D50(const EmArea02OvlHooks *h, uint32_t self, int32_t *result, EmArea02OvlFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA02_OVERLAY_H */
