/* AREA02's delivered overlay (OVERLAY/AREA02.BIN, header id 3) and its area
 * init 0x823900: the AREA00 state module's shape (em_area00_state.h). The
 * six boot globals D_00275C18..2F are borrowed from their one owner
 * (em_area01_state), never copied here; the overlay data and BSS are the
 * module loader's delivered bytes. */
#ifndef EM_AREA02_STATE_H
#define EM_AREA02_STATE_H

#include <stdint.h>

#define EM_AREA02_STATE_GLOBALS 0x00275C18u
#define EM_AREA02_STATE_GLOBAL_SIZE 24u
#define EM_AREA02_STATE_DATA 0x00825680u   /* header + 0x40 + text 0x2140 */
#define EM_AREA02_STATE_DATA_SIZE 0x3C00u
#define EM_AREA02_STATE_BSS 0x00829280u
#define EM_AREA02_STATE_BSS_SIZE 0x281800u

typedef uint8_t *(*EmArea02StateMemory)(void *, uint32_t, uint32_t);

typedef struct {
    EmArea02StateMemory memory;
    void *ctx;
    uint8_t *globals;
    uint32_t fault;
} EmArea02State;

/* Bind only to the delivered AREA02 module (header id 3 with its text,
 * data and BSS sizes). Borrowing initializes or clears nothing. */
int em_area02_state_bind(EmArea02State *, EmArea02StateMemory, void *,
                         uint8_t globals[EM_AREA02_STATE_GLOBAL_SIZE]);
uint8_t *em_area02_state_bytes(EmArea02State *, uint32_t, uint32_t);
void em_area02_state_detach(EmArea02State *);

/* 0x823900, the area init 001E7780 dispatches for keys 0x200..0x202:
 * em_area02_ovl_00823900, its one translation (test_area02_overlay_reference),
 * over these views. The common 001E7780 owner does the surrounding resets. */
int em_area02_state_00823900(EmArea02State *);

#endif
