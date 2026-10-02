/* AREA11 placement record 13: the class-9 manager at overlay 0x008257A0
 * (S12a; SCENE_COORDINATOR_DESIGN.md 4.4 row r13), the whole function.
 *
 * Ground truth: the sibling decomp's byte-identical C,
 * src/overlays/AREA11/func_overlay_AREA11_00825760.c (splat names the
 * function 0x40 below its runtime address; docs/AREA11_OVERLAY.md there).
 * It switches on the node's +4 byte:
 *   0  001BA1C0(self, 0x3C), i.e. D_00810758[0x3C] (D_00810794) == 0xFF:
 *      +4 = 3; otherwise, D_00810788 == 0: +4 = 3; else +4 = 1 and +0 = 1.
 *   1  by +5: 0 waits for 001B1EA0(0, D_00810350, 0x82ACA0, 4) (the player
 *      inside the overlay's area quad 0x82ACA0) and then starts the script
 *      0x829E80 (001BA1A0(self + 0x1F0, 0x829E80)) and sets +5 = 1; 1 polls
 *      it (001BA1F0(self)) and at its end calls 001DFE40, sets +4 = 3 and
 *      D_00810814 = 1; then, on every +5, 001B17A0(self). State 1 is reached
 *      only from state 0 with event 0x30 (D_00810788) set, which only a
 *      return visit sets (no first-level capture shows it).
 *   2, 3  001AFC10(self): the node frees itself (Q3: on the second world
 *      frame after a New Game load).
 *   other  returns.
 * Verified by tools/test_manager_8257a0_reference.py, which executes the
 * overlay code and the main-ELF 001BA1C0 over every state/event/flag case
 * and every callee result, and compares the calls and the bytes.
 */
#ifndef EM_MANAGER_008257A0_H
#define EM_MANAGER_008257A0_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_MANAGER_008257A0 0x008257A0u
#define EM_MANAGER_008257A0_AREA 0x0082ACA0u    /* 001B1EA0's polygon */
#define EM_MANAGER_008257A0_SCRIPT 0x00829E80u  /* 001BA1A0's a1 */

typedef struct {
    uint8_t b00;     /* node +0x00 */
    uint8_t b04;     /* node +0x04 state */
    uint8_t d810794; /* D_00810758[0x3C], read by 001BA1C0 */
    uint8_t d810788; /* event 0x30 */
    uint8_t b05;     /* node +0x05: state 1's step */
} EmManager8257A0;

typedef struct {
    void *ctx;
    int (*w_001AFC10)(void *ctx); /* free the node itself */
    /* 001B1EA0(0, D_00810350, 0x82ACA0, 4): *result nonzero inside. */
    int (*w_001B1EA0)(void *ctx, int32_t *result);
    /* 001BA1A0(self + 0x1F0, entry) / 001BA1F0(self): the script host. */
    int (*w_001BA1A0)(void *ctx, uint32_t entry);
    int (*w_001BA1F0)(void *ctx, int32_t *result);
    int (*w_001DFE40)(void *ctx);
    /* D_00810814 = value. */
    int (*s_00810814)(void *ctx, uint8_t value);
    int (*w_001B17A0)(void *ctx);
} EmManager8257A0Workers;

/* One behaviour call. 0, or -1 when a worker is missing or fails
 * (*fault_address names it). */
int em_manager_008257A0_tick(EmManager8257A0 *m, const EmManager8257A0Workers *w, uint32_t *fault_address);

#ifdef __cplusplus
}
#endif

#endif /* EM_MANAGER_008257A0_H */
