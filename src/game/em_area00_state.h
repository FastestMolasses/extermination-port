/* AREA00's delivered overlay and 00824E00 initializer. The six boot
 * globals are borrowed from their existing owner, never copied here. */
#ifndef EM_AREA00_STATE_H
#define EM_AREA00_STATE_H

#include <stdint.h>

#define EM_AREA00_STATE_GLOBALS 0x00275C18u
#define EM_AREA00_STATE_GLOBAL_SIZE 24u
#define EM_AREA00_STATE_DATA 0x00826F80u
#define EM_AREA00_STATE_DATA_SIZE 0x6500u
#define EM_AREA00_STATE_BSS 0x0082D480u
#define EM_AREA00_STATE_BSS_SIZE 0x295900u

typedef uint8_t *(*EmArea00StateMemory)(void *, uint32_t, uint32_t);

typedef struct {
    EmArea00StateMemory memory;
    void *ctx;
    uint8_t *globals;
    uint32_t fault;
} EmArea00State;

/* Bind only to the delivered AREA00 module. Borrowing does not initialize
 * or clear any game bytes. A missing allocation fails rather than seeds. */
int em_area00_state_bind(EmArea00State *, EmArea00StateMemory, void *,
                        uint8_t globals[EM_AREA00_STATE_GLOBAL_SIZE]);
uint8_t *em_area00_state_bytes(EmArea00State *, uint32_t, uint32_t);
void em_area00_state_detach(EmArea00State *);

/* The original area's six stores, verified by test_area00_state_reference.
 * The existing common 001E7780 owner must perform its preceding reset and
 * following +54/+58 record clears; this entry owns neither operation. */
int em_area00_state_00824E00(EmArea00State *);

#endif
