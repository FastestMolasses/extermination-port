/* AREA11 placement record 13: the class-9 manager at overlay 0x008257A0
 * (S12a; SCENE_COORDINATOR_DESIGN.md 4.4 row r13).
 *
 * Hand translation of the overlay behaviour, read from the AREA11 overlay's
 * splat listing (Extermination build/overlays/AREA11, function
 * func_overlay_AREA11_00825760: splat places the file at 0x823500 - 0x40, the
 * loader at 0x823500, so its 0x825760 is the node's callback 0x8257A0). It
 * switches on the node's +4 byte:
 *   0  001BA1C0(self, 0x3C), i.e. D_00810758[0x3C] (D_00810794) == 0xFF:
 *      +4 = 3; otherwise, D_00810788 == 0: +4 = 3; else +4 = 1 and +0 = 1.
 *   1  the script arm on +5 (001B1EA0 player-region test against the overlay
 *      box 0x82ACE0, 001BA1A0 script start 0x829EC0, 001BA1F0 poll, 001DFE40,
 *      D_00810814 = 1, 001B17A0): NOT translated here. It is reached only
 *      from state 0 with event 0x30 (D_00810788) set, which no AREA11 capture
 *      shows; reaching it faults (WP-10 translates the scripted managers).
 *   2, 3  001AFC10(self): the node frees itself (Q3: on the second world
 *      frame after a New Game load, 0x8258E0 is the return address).
 *   other  returns.
 * Verified by tools/test_manager_8257a0_reference.py, which executes the
 * overlay code and the main-ELF 001BA1C0 over every state/event/flag case.
 */
#ifndef EM_MANAGER_008257A0_H
#define EM_MANAGER_008257A0_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EM_MANAGER_008257A0 0x008257A0u

typedef struct {
    uint8_t b00;     /* node +0x00 */
    uint8_t b04;     /* node +0x04 state */
    uint8_t d810794; /* D_00810758[0x3C], read by 001BA1C0 */
    uint8_t d810788; /* event 0x30 */
} EmManager8257A0;

typedef struct {
    void *ctx;
    int (*w_001AFC10)(void *ctx); /* free the node itself */
} EmManager8257A0Workers;

/* One behaviour call. 0, or -1 when a worker fails or the untranslated
 * state 1 is reached (*fault_address names the function). */
int em_manager_008257A0_tick(EmManager8257A0 *m, const EmManager8257A0Workers *w, uint32_t *fault_address);

#ifdef __cplusplus
}
#endif

#endif /* EM_MANAGER_008257A0_H */
