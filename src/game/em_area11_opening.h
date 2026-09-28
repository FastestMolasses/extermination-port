/* The AREA11 opening controller 00823E80 (overlay AREA11, runtime
 * 0x823E80..0x823FE8), state 1: its script machine. State 0 (001B0FD0,
 * 001C6380, +0x04 = 1, +0x00 = 1) and the tail every state-1 call ends
 * with (001B1B70(self), then its +0x4C) are the binder's
 * (em_area11_bindings.c tick_opening).
 *
 * State 1, read from the original instructions (0x823EF4..0x823FA8):
 *   001BA1C0(self, 0x39) set (D_00810758[0x39] == 0xFF): nothing more;
 *   +0x05 == 0: 001BA1A0(self + 0x1F0, 0x828FC0), 001FABB0(), +0x05 = 1;
 *   +0x05 == 1: 001BA1F0(self); a nonzero result (1 finished, 3 the skip
 *               path) runs the completion: +0x2E = 0xFFFF (the done mask
 *               the script's 001BB0E0 actors read), D_00810811 = 0xFF,
 *               001C4760(0, 1), 001FAE70(0), +0x05 = 2, 001AEE10(4, 0);
 *   any other +0x05: nothing.
 * The callees are workers (the script host, the stream lanes, the key
 * item, the fade); a NULL worker or a negative result is a fault. */
#ifndef EM_AREA11_OPENING_H
#define EM_AREA11_OPENING_H

#include <stdint.h>

#define EM_AREA11_OPENING_SCRIPT 0x00828FC0u   /* 001BA1A0's a1 (0x823F34) */
#define EM_AREA11_OPENING_EVENT 0x39u          /* 001BA1C0's a1 (0x823EF4) */

typedef struct {
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
    /* D_00810811 = value (0x823F74..0x823F80). */
    int (*s_00810811)(void *ctx, uint8_t value);
    int (*w_001C4760)(void *ctx, int32_t a0, int32_t a1);
    int (*w_001FAE70)(void *ctx, int32_t a0);
    int (*w_001AEE10)(void *ctx, int16_t a0, uint8_t a1);
} EmArea11OpeningWorkers;

/* One state-1 call up to the tail. 0, or -1 (*fault_address = the callee
 * that failed or was missing). */
int em_area11_opening_state1(EmArea11Opening *self, const EmArea11OpeningWorkers *w, uint32_t *fault_address);

#endif
