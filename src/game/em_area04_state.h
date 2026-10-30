/* AREA04's delivered overlay (OVERLAY/AREA04.BIN, header id 5) and its area
 * init 0x823B10: the AREA02 state module's shape (em_area02_state.h). The
 * six boot globals D_00275C18..2F are borrowed from their one owner
 * (em_area01_state), never copied here; the overlay data and BSS are the
 * module loader's delivered bytes. */
#ifndef EM_AREA04_STATE_H
#define EM_AREA04_STATE_H

#include <stdint.h>

#define EM_AREA04_STATE_GLOBALS 0x00275C18u
#define EM_AREA04_STATE_GLOBAL_SIZE 24u
#define EM_AREA04_STATE_DATA 0x00826600u   /* header + 0x40 + text 0x30C0 */
#define EM_AREA04_STATE_DATA_SIZE 0x6300u
#define EM_AREA04_STATE_BSS 0x0082C900u
#define EM_AREA04_STATE_BSS_SIZE 0x14AC80u

typedef uint8_t *(*EmArea04StateMemory)(void *, uint32_t, uint32_t);

typedef struct {
    EmArea04StateMemory memory;
    void *ctx;
    uint8_t *globals;
    uint32_t fault;
} EmArea04State;

/* Bind only to the delivered AREA04 module (header id 5 with its text,
 * data and BSS sizes). Borrowing initializes or clears nothing. */
int em_area04_state_bind(EmArea04State *, EmArea04StateMemory, void *,
                         uint8_t globals[EM_AREA04_STATE_GLOBAL_SIZE]);
uint8_t *em_area04_state_bytes(EmArea04State *, uint32_t, uint32_t);
void em_area04_state_detach(EmArea04State *);

/* 0x823B10 (decomp src/overlays/AREA04/func_overlay_AREA04_00823AD0.c),
 * the area init 001E7780 dispatches for keys 0x400 and 0x401: five stores,
 * D_00275C2C = 1, D_00275C28 = 0x20, D_00275C20 = 0x82C900 (the BSS),
 * D_00275C24 = 0, D_00275C1C = 0x836980, in that order. The common 001E7780
 * owner does the surrounding resets. */
int em_area04_state_00823B10(EmArea04State *);

#endif
