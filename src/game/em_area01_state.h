/* AREA01 overlay state and its load-time initializer. The module loader
 * owns the delivered overlay data and BSS; this provider borrows those
 * canonical bytes and owns only D_00275C18..2F. No captured state is loaded.
 * See docs/LEVEL2_RUNTIME.md ("AREA01 canonical overlay state and initialization") for lifecycle and integration order. */
#ifndef EM_AREA01_STATE_H
#define EM_AREA01_STATE_H

#include <stdint.h>

#define EM_AREA01_STATE_GLOBALS 0x00275C18u
#define EM_AREA01_STATE_GLOBAL_SIZE 0x18u
#define EM_AREA01_STATE_DATA 0x00828A00u
#define EM_AREA01_STATE_DATA_SIZE 0x4300u
#define EM_AREA01_STATE_BSS 0x0082CD00u
#define EM_AREA01_STATE_BSS_SIZE 0x14AC80u

/* Return an existing mutable span, never allocate or seed it. The live
 * binding uses em_module_loader_memory_mutable. Resolve on every request:
 * drive reads can replace allocations when another module is loaded. */
typedef uint8_t *(*EmArea01StateMemory)(void *ctx, uint32_t address, uint32_t size);

typedef struct {
    uint8_t globals[EM_AREA01_STATE_GLOBAL_SIZE];
    EmArea01StateMemory memory;
    void *ctx;
    uint32_t fault; /* first failing original address; zero while healthy */
} EmArea01State;

/* Start with a zero-initialized object. Binding validates the delivered
 * AREA01 header and both full spans; it neither reloads nor clears them.
 * Return 0, or -1 with the first fault latched. A fault stays latched until
 * the host explicitly discards this binding with detach. */
int em_area01_state_bind(EmArea01State *s, EmArea01StateMemory memory, void *ctx);
void em_area01_state_detach(EmArea01State *s);

/* Only the three owned/viewed spans above are exposed. Unknown/empty/
 * wrapping ranges, a missing or replaced overlay, or a latched fault
 * return NULL. Queries do not latch faults, so a composite binder can try
 * its other canonical owners. Never retain a view across a module load. */
uint8_t *em_area01_state_bytes(EmArea01State *s, uint32_t address, uint32_t size);

/* Runtime 00823A50 (source func_overlay_AREA01_00823A10.c): six stores.
 * 001E7780 below supplies the original surrounding zero/record-clear order.
 * Both return 0, or -1 with a latched original address. */
int em_area01_state_00823A50(EmArea01State *s);
/* AREA01 portion of the shared dispatcher. area must be 1; sub 0/1 call
 * the initializer, other sub bytes have no original dispatch target and
 * only clear the six globals. Other areas fail before changing globals. */
int em_area01_state_001E7780(EmArea01State *s, uint8_t area, uint8_t sub);

#endif
