/* AREA01 overlay owners: standalone translations of the 14 AREA01 overlay
 * (OVERLAY/AREA01.BIN, id 2) functions the AREA01 route ran that the first
 * level never ran (decomp census build/s87/census/a01_delta.json,
 * region overlay:AREA01). Docs: docs/AREA01_OVERLAY.md.
 *
 * Addresses are engine (runtime) addresses. The overlay is linked 0x40 below
 * where it runs, so the decomp's splat/link names are 0x40 lower (for
 * example 0x825350 is func_overlay_AREA01_00825310).
 *
 * Ground truth: the decomp's byte-identical C under
 * src/overlays/AREA01/ for twelve of them; 0x823580 and 0x826D40 are still
 * assembly in the decomp and were translated from the original code. Every
 * function is compared with the ORIGINAL overlay code, run over the recorded
 * AREA01 RAM images, by tools/test_area01_overlay_reference.py: callee calls
 * and arguments, memory at every call entry, the memory accesses between
 * calls, final RAM and scratchpad, and return values, on the cases that test
 * runs. docs/AREA01_OVERLAY.md section 3 says exactly what that covers and
 * which mutants it was checked against; nothing beyond that is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory (actor records, globals, overlay data, scratchpad 0x70000000..) goes
 * through `bytes`, which maps an original address range to native bytes laid
 * out as the original (little-endian, original offsets). Pointers stored in
 * records are original 32-bit addresses and are resolved through `bytes`
 * again, exactly where the original dereferences them.
 *
 * Callees: every boot function, every overlay function outside this set, and
 * the actor's +0x4C callback are hooks named by original address. Pointer
 * arguments are original addresses. A hook returns >= 0 on success; its
 * original result (when the original has one) is written to *result. The
 * module never simulates a callee. `ctx` is passed unchanged to `bytes`,
 * every hook and the callback, and never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * an op09 entry does not write *result, and the entry returns -1. A fault
 * already latched on entry makes the call return -1 without doing
 * anything; so do a NULL hook table and a NULL fault pointer. Which of
 * these the test checks, and how, is docs/AREA01_OVERLAY.md section 3
 * ("Native fail-stop checks", "Hook contract"); a hook that re-enters an
 * entry with the same fault record is not covered.
 *
 * Unbound: nothing in the port calls these entries yet (level-2 side track;
 * the binding chain comes after the first level).
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA01_OVERLAY_H
#define EM_AREA01_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry addresses (placement / group behaviour at actor +0x10, script op09
 * callback at record +0x04, or overlay-internal call target). */
#define EM_AREA01_OVL_SHAFT_DOOR   0x00823580u /* placement [12] owner */
#define EM_AREA01_OVL_825130       0x00825130u /* op09 callback */
#define EM_AREA01_OVL_825240       0x00825240u /* op09 callback */
#define EM_AREA01_OVL_NPC          0x00825350u /* placement [36] owner */
#define EM_AREA01_OVL_8254B0       0x008254B0u /* called by 0x825350 */
#define EM_AREA01_OVL_825590       0x00825590u /* called by 0x825350 */
#define EM_AREA01_OVL_825670       0x00825670u /* called by 0x825350 */
#define EM_AREA01_OVL_8261A0       0x008261A0u /* placements [41]/[42] owner */
#define EM_AREA01_OVL_826200       0x00826200u /* called by 0x8261A0 (+0xD == 2) */
#define EM_AREA01_OVL_826440       0x00826440u /* called by 0x8261A0 (+0xD == 3) */
#define EM_AREA01_OVL_8267C0       0x008267C0u /* placement [45] owner */
#define EM_AREA01_OVL_826CF0       0x00826CF0u /* placement [37] / group owner */
#define EM_AREA01_OVL_826D40       0x00826D40u /* deferred group owner */
#define EM_AREA01_OVL_828850       0x00828850u /* deferred group owner */

enum {
    EM_AREA01_OVL_FAULT_NONE = 0,
    EM_AREA01_OVL_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA01_OVL_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA01_OVL_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, or the unmapped data address */
    int32_t code;     /* EM_AREA01_OVL_FAULT_* */
} EmArea01OvlFault;

typedef struct {
    void *ctx;
    /* Original memory view: native bytes for [address, address + size), or
     * NULL when the range is not mapped (fault). */
    uint8_t *(*bytes)(void *ctx, uint32_t address, uint32_t size);

    /* ---- boot callees ------------------------------------------------ */
    int (*w_00102760)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_001028D0)(void *ctx, uint32_t dst, uint32_t a, uint32_t b);
    int (*w_00102948)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_00102958)(void *ctx, uint32_t dst, uint32_t src);
    int (*w_0011DBB8)(void *ctx, float x, float *result);
    int (*w_0011DF78)(void *ctx, float x, float *result);
    int (*w_0011E2A8)(void *ctx, float x, float *result);
    int (*w_0011E520)(void *ctx, float x, float *result);
    int (*w_0011E748)(void *ctx, float x, float *result);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00182BF0)(void *ctx, uint32_t actor, int32_t *result);
    /* a2 is the word 0x826D40 last loaded into that register (the pointer
     * at 0x700031D0); passed because the callee may read it. */
    int (*w_0019B6C0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result);
    int (*w_001A2370)(void *ctx, uint32_t actor, uint32_t matrix);
    int (*w_001AFA90)(void *ctx, int32_t cls, uint32_t *result);
    int (*w_001AFC10)(void *ctx, uint32_t actor);
    int (*w_001B0FD0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t actor, int32_t a1, int32_t a2, int32_t *result);
    int (*w_001B1190)(void *ctx, int32_t a0);
    int (*w_001B11E0)(void *ctx, int32_t a0, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t origin, float x, float z, float *result);
    int (*w_001B12B0)(void *ctx, float goal, float current, float rate, float *result);
    int (*w_001B1380)(void *ctx, uint32_t a0, uint32_t a1, float f12, int32_t *result);
    int (*w_001B1470)(void *ctx, float angle, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t actor);
    int (*w_001B1EA0)(void *ctx, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result);
    int (*w_001BA1A0)(void *ctx, uint32_t block, uint32_t script);
    int (*w_001BA1C0)(void *ctx, uint32_t actor, int32_t index, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t actor, int32_t *result);
    int (*w_001BA540)(void *ctx, uint32_t actor);
    int (*w_001BA580)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001BA8E0)(void *ctx, uint32_t actor, int32_t type);
    int (*w_001BBDA0)(void *ctx, uint32_t actor);
    int (*w_001BBE40)(void *ctx, uint32_t actor, uint32_t block, int32_t mode, int32_t *result);
    int (*w_001BC0E0)(void *ctx, uint32_t actor, uint32_t block, int32_t *result);
    int (*w_001BC240)(void *ctx, uint32_t actor, uint32_t block);
    int (*w_001BC290)(void *ctx, uint32_t actor, uint32_t block, int32_t *result);
    int (*w_001BC300)(void *ctx, uint32_t actor);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1, int32_t *result);
    int (*w_001C4820)(void *ctx, uint32_t actor);
    int (*w_001C5C90)(void *ctx, uint32_t actor);
    int (*w_001C6380)(void *ctx, uint32_t actor);
    int (*w_001C63E0)(void *ctx, uint32_t actor, int32_t a1);
    int (*w_001C64F0)(void *ctx, uint32_t actor, float dt, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t actor, int32_t clip, float blend, float frame);
    int (*w_001C68C0)(void *ctx, uint32_t actor);
    int (*w_001EFD90)(void *ctx, int32_t id, uint32_t position, uint32_t rotation, int32_t *result);
    int (*w_001EFE00)(void *ctx, int32_t a0, uint32_t a1, int32_t *result);
    int (*w_001FBD50)(void *ctx, uint32_t actor, int32_t cue, int32_t a2, float f12, int32_t *result);
    int (*w_001FC3C0)(void *ctx, uint32_t actor, uint32_t handle, int32_t cue, float f12, float f13);
    int (*w_001FC520)(void *ctx, uint32_t handle);

    /* ---- AREA01 overlay callees outside this module ------------------ */
    int (*w_008282F0)(void *ctx, uint32_t actor, uint32_t matrix, int32_t *result);
    int (*w_008287C0)(void *ctx, uint32_t matrix);

    /* ---- the actor's own callback: the function at actor +0x4C, called with the actor */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea01OvlHooks;

/* Placement / group owners: one call of the original behaviour. Return 0,
 * or -1 on a fault. */
int em_area01_ovl_00823580(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00825350(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_008261A0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_008267C0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00826CF0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00826D40(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00828850(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);

/* Overlay-internal callees (also reached through the owners above). */
int em_area01_ovl_008254B0(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00825590(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00825670(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00826200(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);
int em_area01_ovl_00826440(const EmArea01OvlHooks *h, uint32_t self, EmArea01OvlFault *fault);

/* Script op09 callbacks (owner, script block, record); *result receives the
 * original return value (1 = done, 0 = keep running); it is left unwritten
 * when the call returns -1. */
int em_area01_ovl_00825130(const EmArea01OvlHooks *h, uint32_t self, uint32_t block,
                           uint32_t record, int32_t *result, EmArea01OvlFault *fault);
int em_area01_ovl_00825240(const EmArea01OvlHooks *h, uint32_t self, uint32_t block,
                           uint32_t record, int32_t *result, EmArea01OvlFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA01_OVERLAY_H */
