/* AREA04 owners and two boot functions: standalone translations of the 16
 * functions the AREA04 route census found new (decomp
 * build/s87/census/a04_delta.json, new_functions: 14 in the AREA04 overlay,
 * OVERLAY/AREA04.BIN header id 5, and the boot functions 001BE5F0 and
 * 001C1A80, which first ran right after the AREA22 arrival), plus the three
 * overlay functions they call that have no port translation: 0x823580
 * (called by 0x823700) and 0x824830 / 0x824930 (called by 0x8246B0).
 * Docs: docs/AREA04_PORT.md.
 *
 * Addresses are engine (runtime) addresses. The overlay is linked 0x40
 * below where it runs, so the decomp's splat/link names are 0x40 lower
 * (0x823B90 is func_overlay_AREA04_00823B50).
 *
 * Ground truth: the decomp's C under src/overlays/AREA04/ (byte-identical
 * for all 17 overlay functions here; decomp docs/AREA04_OVERLAY.md), the
 * decomp's src/func_001BE5F0.c (inline asm) and src/func_001C1A80.c
 * (NEARMISS: its body was checked against the original instructions, and
 * where the NEARMISS text and the instructions differ the instructions
 * win: 001D0D60 takes a0 and f12 only). Every function is compared with the
 * ORIGINAL code, run over recorded RAM images, by
 * tools/test_area04_port_reference.py: callee calls and arguments, memory
 * at every call entry, the memory accesses between calls, final RAM,
 * scratchpad and stack locals, and return values, on the cases that test
 * runs. docs/AREA04_PORT.md section 3 says exactly what that covers;
 * nothing beyond that is claimed.
 *
 * Memory model: the module owns no state. Every read and write of original
 * memory (actor records, globals, overlay data, scratchpad 0x70000000.. and
 * the stack locals of 0x824490) goes through `bytes`, which maps an
 * original address range to native bytes laid out as the original
 * (little-endian, original offsets). Pointers stored in records are
 * original 32-bit addresses and are resolved through `bytes` again, exactly
 * where the original dereferences them.
 *
 * Calls between the translations: 0x823700 -> 0x823580, 0x8246B0 ->
 * 0x824830 / 0x824930, 001C1A80 -> 001BE5F0. They call each other directly
 * (no hook); every function is also a public entry.
 *
 * Stack locals: 0x824490 copies the 64-byte area at 0x828220 into its own
 * frame (sp - 0x40 .. sp - 0x01) and passes its address to 001B1EA0. Its
 * entry takes `sp`, the original stack pointer at entry (16-byte aligned,
 * as on the EE); the caller must map [sp - 0x40, sp) for the module.
 *
 * Callees: every other boot function and the actor's +0x4C callback are
 * hooks named by original address. Pointer arguments are original
 * addresses. A hook returns >= 0 on success; its original result (when the
 * original uses one) is written to *result (float results as the float
 * whose bits are the original's). The module never simulates a callee.
 * `ctx` is passed unchanged to `bytes`, every hook and the callback, and
 * never dereferenced.
 *
 * Fail-stop: a NULL `bytes` mapping, a reached NULL hook or a negative hook
 * result latches a fault (address + code); from then on no hook is called,
 * `bytes` is not called, writes are discarded, the latched fault is kept,
 * an entry with a result does not write *result, and the entry returns -1.
 * A fault already latched on entry makes the call return -1 without doing
 * anything; so do a NULL hook table, a NULL fault pointer and (for the
 * entries with a result) a NULL result pointer.
 *
 * Unbound: nothing in the port calls these entries yet (level-3/4 side
 * track; docs/AREA04_PORT.md "Binding").
 *
 * stdint only; floats follow the EE model of em_ee_float.h.
 */
#ifndef EM_AREA04_PORT_H
#define EM_AREA04_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entry addresses. Roles are those the route capture measured
 * (docs/FIFTH_LEVEL_ROUTE.md sections 1.1 and 2.3-2.4); the code does not
 * name them. */
#define EM_AREA04_PORT_823580 0x00823580u /* talk turn (from 0x823700); returns 1 on a Use */
#define EM_AREA04_PORT_823700 0x00823700u /* door [45]: the door lifecycle with the talk turn */
#define EM_AREA04_PORT_823B40 0x00823B40u /* script callback: D_00810845 bit 3 once; returns 1 */
#define EM_AREA04_PORT_823B90 0x00823B90u /* the director [1] */
#define EM_AREA04_PORT_823EE0 0x00823EE0u /* [47]: second model kind from 0x827534 at the event */
#define EM_AREA04_PORT_824100 0x00824100u /* the group 0x8268D0 node */
#define EM_AREA04_PORT_8241F0 0x008241F0u /* [61]: script 0x827D10 on D_0081083D */
#define EM_AREA04_PORT_824490 0x00824490u /* the NPC [2]'s first sub-state (from 0x824320) */
#define EM_AREA04_PORT_8246B0 0x008246B0u /* [3]: dispatch by D_008107EA */
#define EM_AREA04_PORT_824830 0x00824830u /* [3]'s sub-state +5 (from 0x8246B0) */
#define EM_AREA04_PORT_824930 0x00824930u /* [3]'s sub-state +6 (from 0x8246B0) */
#define EM_AREA04_PORT_824DC0 0x00824DC0u /* [4]: script 0x828BE0 on D_0081076C */
#define EM_AREA04_PORT_825880 0x00825880u /* [66]: Use script 0x82C1F0 and a marker (001F4BF0) */
#define EM_AREA04_PORT_825B00 0x00825B00u /* [65]: +0xB4 moves between base and base + 10 (D_00810834) */
#define EM_AREA04_PORT_825D60 0x00825D60u /* [68]: 001C6380 once, then the callback */
#define EM_AREA04_PORT_825DF0 0x00825DF0u /* the console [69] */
#define EM_AREA04_PORT_8260C0 0x008260C0u /* the cable reel [70] */
#define EM_AREA04_PORT_1BE5F0 0x001BE5F0u /* boot: player within a record's radius and height; returns 0 / 1 */
#define EM_AREA04_PORT_1C1A80 0x001C1A80u /* boot: owner with a bone record, a timed sound and a contact push */

enum {
    EM_AREA04_PORT_FAULT_NONE = 0,
    EM_AREA04_PORT_FAULT_NULL_WORKER = 1,   /* reached hook is NULL */
    EM_AREA04_PORT_FAULT_WORKER_FAILED = 2, /* hook returned < 0 */
    EM_AREA04_PORT_FAULT_BAD_ADDRESS = 5    /* `bytes` could not map an address */
};

typedef struct {
    uint32_t address; /* original callee, or the unmapped data address */
    int32_t code;     /* EM_AREA04_PORT_FAULT_* */
} EmArea04PortFault;

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
    int (*w_001028D0)(void *ctx, uint32_t a0, uint32_t a1, uint32_t a2);
    int (*w_00102900)(void *ctx, uint32_t a0, uint32_t a1, float f2);
    int (*w_00102948)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00121870)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_00122BB8)(void *ctx, int32_t *result);
    int (*w_00182F90)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00183010)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_00187EC0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001A2370)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001AF890)(void *ctx, uint32_t a0);
    int (*w_001AFC10)(void *ctx, uint32_t a0);
    int (*w_001B0FD0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B10B0)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, int32_t *result);
    int (*w_001B1240)(void *ctx, uint32_t a0, float f1, float f2, float *result);
    int (*w_001B1470)(void *ctx, float f0, float *result);
    int (*w_001B17A0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001B1B70)(void *ctx, uint32_t a0);
    int (*w_001B1DE0)(void *ctx, uint32_t a0);
    int (*w_001B1E20)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001B1EA0)(void *ctx, int32_t n0, uint32_t a1, uint32_t a2, int32_t n3, int32_t *result);
    int (*w_001B6660)(void *ctx, uint32_t a0, uint32_t *result);
    int (*w_001B6F80)(void *ctx, uint32_t a0, float f1);
    int (*w_001BA1A0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BA1C0)(void *ctx, uint32_t a0, int32_t n1, int32_t *result);
    int (*w_001BA1F0)(void *ctx, uint32_t a0, int32_t *result);
    int (*w_001BA580)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001BB520)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001BB560)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t *result);
    int (*w_001BB7C0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BB7F0)(void *ctx, uint32_t a0, uint32_t a1, int32_t *result);
    int (*w_001BC150)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001C4760)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C47A0)(void *ctx, int32_t n0, int32_t n1);
    int (*w_001C5570)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3, uint32_t *result);
    int (*w_001C6120)(void *ctx, uint32_t a0, int32_t n1, uint32_t *result);
    int (*w_001C62C0)(void *ctx, uint32_t a0);
    int (*w_001C6380)(void *ctx, uint32_t a0);
    int (*w_001C63E0)(void *ctx, uint32_t a0, int32_t n1);
    int (*w_001C64F0)(void *ctx, uint32_t a0, float f1, int32_t *result);
    int (*w_001C67E0)(void *ctx, uint32_t a0, int32_t n1, float f2, float f3);
    int (*w_001C68C0)(void *ctx, uint32_t a0);
    int (*w_001CA5E0)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2);
    int (*w_001CA6E0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001D0C80)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001D0D40)(void *ctx, uint32_t a0, uint32_t a1, int32_t n2, int32_t n3);
    int (*w_001D0D60)(void *ctx, uint32_t a0, float f1);
    int (*w_001EFD20)(void *ctx, int32_t n0, uint32_t a1, uint32_t *result);
    int (*w_001F4BF0)(void *ctx, uint32_t a0, uint32_t a1);
    int (*w_001FABB0)(void *ctx);
    int (*w_001FAE70)(void *ctx, int32_t n0);
    int (*w_001FB0B0)(void *ctx, int32_t n0);
    int (*w_001FB9F0)(void *ctx, int32_t n0, int32_t n1, int32_t n2, int32_t n3);
    int (*w_001FBD50)(void *ctx, uint32_t a0, int32_t n1, int32_t n2, float f3);

    /* ---- the actor's own callback: the function at actor +0x4C, called with the actor */
    int (*w_callback)(void *ctx, uint32_t function, uint32_t actor);
} EmArea04PortHooks;

/* Owners: every entry returns 0, or -1 on a fault. */
int em_area04_port_00823700(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00823B90(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00823EE0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00824100(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_008241F0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00824490(const EmArea04PortHooks *h, uint32_t self, uint32_t sp, EmArea04PortFault *fault);
int em_area04_port_008246B0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00824830(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00824930(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00824DC0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00825880(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00825B00(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00825D60(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_00825DF0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_008260C0(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);
int em_area04_port_001C1A80(const EmArea04PortHooks *h, uint32_t self, EmArea04PortFault *fault);

/* Entries with a result: *result receives the original return value; it
 * is left unwritten when the call returns -1. 0x823580 takes the talk
 * block `who` (0x823700 passes self + 0x1F0); 001BE5F0 takes the player
 * block, the actor and the actor's +0x1F0 block, in the original's order. */
int em_area04_port_00823580(const EmArea04PortHooks *h, uint32_t self, uint32_t who, int32_t *result,
                            EmArea04PortFault *fault);
int em_area04_port_00823B40(const EmArea04PortHooks *h, int32_t *result, EmArea04PortFault *fault);
int em_area04_port_001BE5F0(const EmArea04PortHooks *h, uint32_t player, uint32_t actor, uint32_t block,
                            int32_t *result, EmArea04PortFault *fault);

#ifdef __cplusplus
}
#endif

#endif /* EM_AREA04_PORT_H */
