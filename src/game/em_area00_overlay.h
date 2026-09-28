/* AREA00 overlay owners: standalone translations of the 13 AREA00 overlay
 * (OVERLAY/AREA00.BIN, header id 1) functions that ran on the recorded
 * AREA00 route (decomp census build/s87/census/a00_delta.json,
 * new_functions, region overlay:AREA00). Docs: docs/AREA00_OVERLAY_PORT.md.
 *
 * Addresses are engine (runtime) addresses. The overlay is linked 0x40 below
 * where it runs, so the decomp's splat/link names are 0x40 lower (for
 * example 0x825170 is func_overlay_AREA00_00825130).
 *
 * Ground truth: the decomp's C under src/overlays/AREA00/, byte-identical
 * for all thirteen (docs/AREA00_OVERLAY.md in the decomp). Every function is
 * compared with the ORIGINAL overlay code, run over the recorded AREA00 RAM
 * images, by tools/test_area00_overlay_reference.py: callee calls and
 * arguments, memory at every call entry, the memory accesses between calls,
 * final RAM and scratchpad, and return values, on the cases that test runs.
 * docs/AREA00_OVERLAY_PORT.md section 3 says exactly what that covers;
 * nothing beyond that is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory (actor records, globals, overlay data, scratchpad 0x70000000..) goes
 * through `bytes`, which maps an original address range to native bytes laid
 * out as the original (little-endian, original offsets). Pointers stored in
 * records are original 32-bit addresses and are resolved through `bytes`
 * again, exactly where the original dereferences them.
 *
 * Stack locals: 0x823820 and 0x825600 pass the address of a local of their
 * own stack frame to callees (a packet block, a vector). Those two entries
 * take `sp`, the original stack pointer at entry, and pass the local's
 * original address (sp - 0x60 and sp - 0x10, from the original frame
 * layout). The module never reads or writes that memory itself: only the
 * callees do, so the caller must map the frame region for the callees.
 *
 * Callees: every boot function and the actor's +0x4C callback are hooks
 * named by original address. Pointer arguments are original addresses. A
 * hook returns >= 0 on success; its original result (when the original has
 * one) is written to *result. The module never simulates a callee. `ctx` is
 * passed unchanged to `bytes`, every hook and the callback, and never
 * dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * the op09 entry does not write *result, and the entry returns -1. A fault
 * already latched on entry makes the call return -1 without doing anything;
 * so do a NULL hook table and a NULL fault pointer. Which of these the test
 * checks, and how, is docs/AREA00_OVERLAY_PORT.md section 3; a hook that
 * re-enters an entry with the same fault record is not covered.
 *
 * Unbound: nothing in the port calls these entries yet (level-3 side track;
 * docs/AREA00_OVERLAY_PORT.md "Binding").
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA00_OVERLAY_H
#define EM_AREA00_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry addresses (placement / group behaviour at actor +0x10, or a script
 * op09 callback). The roles are those the AREA00 route capture measured
 * (docs/THIRD_LEVEL_ROUTE.md section 1 and 2.4); the code does not name them. */
#define EM_AREA00_OVL_SHAFT_DOOR 0x00823580u /* placement [52] owner */
#define EM_AREA00_OVL_PUFFS      0x00823820u /* spawned while script 0x82A540 runs (a00_08) */
#define EM_AREA00_OVL_ARRIVAL    0x00824EA0u /* the arrival owner (script 0x828D60) */
#define EM_AREA00_OVL_DOOR51     0x00825170u /* placement [51] owner */
#define EM_AREA00_OVL_8253E0     0x008253E0u /* op09 callback of script 0x8299E0 */
#define EM_AREA00_OVL_TERMINAL   0x00825480u /* placement [40] owner */
#define EM_AREA00_OVL_FERRY      0x00825600u /* placements [48]/[49]/[50] owner */
#define EM_AREA00_OVL_825920     0x00825920u /* placement [43] and two group nodes */
#define EM_AREA00_OVL_825C80     0x00825C80u /* examine owner, +0x30 = 0x2758A8 */
#define EM_AREA00_OVL_8261E0     0x008261E0u /* examine owner, +0x30 = 0x2758B0 */
#define EM_AREA00_OVL_8262D0     0x008262D0u /* examine owner, +0x30 = 0x2758B8 */
#define EM_AREA00_OVL_FERRY_CAB  0x008263C0u /* placement [47] owner */
#define EM_AREA00_OVL_BEAM       0x008266A0u /* placement [60] owner */

enum {
    EM_AREA00_OVL_FAULT_NONE = 0,
    EM_AREA00_OVL_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA00_OVL_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA00_OVL_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, or the unmapped data address */
    int32_t code;     /* EM_AREA00_OVL_FAULT_* */
} EmArea00OvlFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees, by original address -------------------------- */
    int (*w_001026A0)(void *ctx, uint32_t dst, uint32_t matrix, uint32_t vector);
    int (*w_001028B8)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_001028D0)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_00102948)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_00102958)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_001029C0)(void *ctx, uint32_t matrix);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_001281C0)(void *ctx, float x, int32_t *result);
    int (*w_00129780)(void *ctx, uint32_t actor, uint32_t block, int32_t sel, int32_t *result);
    int (*w_00182F90)(void *ctx, uint32_t actor, uint32_t point);
    int (*w_0019C6F0)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001A2370)(void *ctx, uint32_t actor, uint32_t matrix);
    int (*w_001AF800)(void *ctx, uint32_t actor);
    int (*w_001AFC10)(void *ctx, uint32_t actor);
    int (*w_001B0C60)(void *ctx, int32_t a0, int32_t a1, int32_t a2);
    int (*w_001B0F60)(void *ctx, uint32_t actor, int32_t a1, int32_t *result);
    int (*w_001B0FD0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t actor, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B17A0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t actor);
    int (*w_001B1E20)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001B6660)(void *ctx, uint32_t record, uint32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t block, uint32_t script);
    int (*w_001BA1C0)(void *ctx, uint32_t actor, int32_t index, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001BBDA0)(void *ctx, uint32_t actor);
    int (*w_001BBE40)(void *ctx, uint32_t actor, uint32_t block, int32_t mode, int32_t *result);
    int (*w_001BC0E0)(void *ctx, uint32_t actor, uint32_t block, int32_t *result);
    int (*w_001BC240)(void *ctx, uint32_t actor, uint32_t block);
    int (*w_001BC290)(void *ctx, uint32_t actor, uint32_t block, int32_t *result);
    int (*w_001BC300)(void *ctx, uint32_t actor);
    int (*w_001C2770)(void *ctx, uint32_t actor, uint32_t block, int32_t a2, int32_t *result);
    int (*w_001C3D60)(void *ctx, uint32_t actor, uint32_t block);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001C5570)(void *ctx, uint32_t actor, uint32_t vector, int32_t a2, int32_t a3);
    int (*w_001C6380)(void *ctx, uint32_t actor);
    int (*w_001C63E0)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t actor, float dt, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t actor, int32_t clip, float blend, float frame);
    int (*w_001C68C0)(void *ctx, uint32_t actor);
    int (*w_001C69A0)(void *ctx, uint32_t actor);
    int (*w_001CB5B0)(void *ctx, int32_t a0);
    int (*w_001CCF70)(void *ctx, uint32_t point, int32_t *result);
    int (*w_001CD520)(void *ctx, int32_t bucket, int32_t mode, uint32_t point, uint64_t giftag, uint32_t rgba, float w, float h, float zbias, int32_t *result);
    int (*w_001CFA60)(void *ctx, uint32_t packet, uint32_t matrix, float f12, float f13);
    int (*w_001CFBE0)(void *ctx, int32_t a0, int32_t a1, uint32_t a2, uint32_t a3, int32_t t0);
    int (*w_001EFD20)(void *ctx, int32_t a0, uint32_t point);
    int (*w_001F4E20)(void *ctx, uint32_t point, uint32_t colour, float f12);
    int (*w_001F5940)(void *ctx, int32_t a0, uint32_t point, int32_t a2);
    int (*w_001FB9F0)(void *ctx, int32_t id, int32_t a1, int32_t a2, int32_t a3);
    int (*w_001FC3C0)(void *ctx, uint32_t actor, uint32_t handle, int32_t cue, float f12, float f13);

    /* ---- the actor's own callback: the function at actor +0x4C, called with the actor */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea00OvlHooks;

/* Placement / group owners: one call of the original behaviour. Return 0,
 * or -1 on a fault. */
int em_area00_ovl_00823580(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_00823820(const EmArea00OvlHooks *h, uint32_t self, uint32_t sp, EmArea00OvlFault *fault);
int em_area00_ovl_00824EA0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_00825170(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_00825480(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_00825600(const EmArea00OvlHooks *h, uint32_t self, uint32_t sp, EmArea00OvlFault *fault);
int em_area00_ovl_00825920(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_00825C80(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_008261E0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_008262D0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_008263C0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);
int em_area00_ovl_008266A0(const EmArea00OvlHooks *h, uint32_t self, EmArea00OvlFault *fault);

/* Script op09 callback (a0, a1 = the script block whose +0x04 / +0x05 it
 * steps, a2): the original reads only a1; the other two are taken so the
 * entry has the op09 calling shape. *result receives the original return
 * value (1 = done, 0 = keep running); it is left unwritten when the call
 * returns -1. */
int em_area00_ovl_008253E0(const EmArea00OvlHooks *h, uint32_t a0, uint32_t block, uint32_t a2,
                           int32_t *result, EmArea00OvlFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA00_OVERLAY_H */
