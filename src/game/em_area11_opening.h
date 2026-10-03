/* The AREA11 opening controller 00823E80 (overlay AREA11, runtime
 * 0x823E80..0x823FE8): the whole function, by its +0x04. Ground truth: the
 * sibling decomp's byte-identical src/overlays/AREA11/
 * func_overlay_AREA11_00823E40.c (docs/AREA11_OVERLAY.md there); oracle:
 * tools/test_area11_opening_reference.py, which executes the original
 * instructions.
 *
 *   +0x04 == 0  001B0FD0(self) (the canopy's model, per-area model 0x11, and
 *               its slot); when it returns 0: 001C6380(self), +0x04 = 1,
 *               +0x00 = 1. Nothing else (no tail).
 *   +0x04 == 1  001BA1C0(self, 0x39) clear (D_00810758[0x39] != 0xFF):
 *                 +0x05 == 0: 001BA1A0(self + 0x1F0, 0x828FC0), 001FABB0(),
 *                             +0x05 = 1;
 *                 +0x05 == 1: 001BA1F0(self); a nonzero result (1 finished,
 *                             3 the skip path) runs the completion:
 *                             +0x2E = 0xFFFF (the done mask the script's
 *                             001BB0E0 actors read), D_00810811 = 0xFF,
 *                             001C4760(0, 1), 001FAE70(0), +0x05 = 2,
 *                             001AEE10(4, 0);
 *                 any other +0x05: nothing;
 *               then on every path 001B1B70(self) and the +0x4C method.
 *   +0x04 == 2 or 3  001AFC10(self) (the record frees itself).
 *   any other +0x04  returns.
 * The callees are workers; a NULL worker or a negative result is a fault.
 * The binder (em_area11_bindings.c tick_opening) owns the record. */
#ifndef EM_AREA11_OPENING_H
#define EM_AREA11_OPENING_H

#include <stdint.h>

#define EM_AREA11_OPENING_CALLBACK 0x00823E80u
#define EM_AREA11_OPENING_SCRIPT 0x00828FC0u   /* 001BA1A0's a1 */
#define EM_AREA11_OPENING_EVENT 0x39u          /* 001BA1C0's a1 */

typedef struct {
    uint8_t b00;       /* +0x00 */
    uint8_t b04;       /* +0x04: the lifecycle */
    uint8_t b05;       /* +0x05: the script machine's state */
    uint16_t h2E;      /* +0x2E: 0xFFFF at the completion */
} EmArea11Opening;

typedef struct {
    void *ctx;
    /* 001BA1C0(self, a1): 1 when D_00810758[a1] == 0xFF. */
    int (*w_001BA1C0)(void *ctx, uint32_t a1, int32_t *result);
    /* 001BA1A0(self + 0x1F0, entry) / 001BA1F0(self): the script host. */
    int (*w_001BA1A0)(void *ctx, uint32_t entry);
    int (*w_001BA1F0)(void *ctx, int32_t *result);
    int (*w_001FABB0)(void *ctx);
    /* D_00810811 = value. */
    int (*s_00810811)(void *ctx, uint8_t value);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001AEE10)(void *ctx, int16_t a0, uint8_t a1);
    /* 001B0FD0(self): *result 0 bound, nonzero refused. */
    int (*w_001B0FD0)(void *ctx, int32_t *result);
    int (*w_001C6380)(void *ctx);
    int (*w_001B1B70)(void *ctx);
    /* The record's +0x4C method, called with self. */
    int (*m_4C)(void *ctx);
    /* 001AFC10(self): after it the record is gone (the caller must not
     * write it back). */
    int (*w_001AFC10)(void *ctx);
} EmArea11OpeningWorkers;

/* One call. 0, or -1 (*fault_address = the callee that failed or was
 * missing; 0x823E80 for a NULL argument). */
int em_area11_opening_tick(EmArea11Opening *self, const EmArea11OpeningWorkers *w, uint32_t *fault_address);

#endif
