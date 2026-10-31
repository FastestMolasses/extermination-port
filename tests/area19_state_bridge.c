/* Test-only loader access for the AREA19 state (as tests/area00_state_bridge.c). */
#include "../src/game/em_module_loader.c"
#include "game/em_area01_state.h"

/* the loader's attachment bank hook: no sound in this test */
void em_sfx_set_attachment(int index) { (void)index; }

static EmModuleLoader loader;
static EmArea01State state;

static uint8_t *memory(void *ctx, uint32_t address, uint32_t size)
{ return em_module_loader_memory_mutable(ctx, address, size); }

void area19_state_test_shutdown(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) free(loader.regions[i].bytes);
    memset(&loader, 0, sizeof loader);
    em_area01_state_detach(&state);
}

int area19_state_test_seed(const uint8_t *overlay, uint32_t size, const uint8_t *bss,
                           const uint8_t *initial_globals)
{
    area19_state_test_shutdown();
    uint8_t *file = region_for(&loader, 0x823500u, size);
    uint8_t *zero = region_for(&loader, EM_AREA01_STATE_A19_BSS, EM_AREA01_STATE_A19_BSS_SIZE);
    if (!file || !zero) return -1;
    memcpy(file, overlay, size);
    memcpy(zero, bss, EM_AREA01_STATE_A19_BSS_SIZE);
    memcpy(state.globals, initial_globals, sizeof state.globals);
    return em_area01_state_bind_area(&state, memory, &loader, 0x13);
}
uint8_t *area19_state_test_view(uint32_t address, uint32_t size)
{ return em_area01_state_bytes(&state, address, size); }
int area19_state_test_init(void) { return em_area01_state_008250C0(&state); }
int area19_state_test_dispatch(uint8_t area, uint8_t sub) { return em_area01_state_001E7780(&state, area, sub); }
uint32_t area19_state_test_fault(void) { return state.fault; }
