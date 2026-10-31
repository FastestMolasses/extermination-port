/* Test-only loader access for AREA15's and AREA03's state (as
 * tests/area04_state_bridge.c). */
#include "../src/game/em_module_loader.c"
#include "game/em_area01_state.h"

/* The loader's sound scope (em_sfx.c is not linked here). */
void em_sfx_set_attachment(int index)
{
    (void)index;
}

static EmModuleLoader loader;
static EmArea01State state;

static uint8_t *memory(void *ctx, uint32_t address, uint32_t size)
{ return em_module_loader_memory_mutable(ctx, address, size); }

void later_state_test_shutdown(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) free(loader.regions[i].bytes);
    memset(&loader, 0, sizeof loader);
    em_area01_state_detach(&state);
}

int later_state_test_seed(unsigned area, const uint8_t *overlay, uint32_t size, const uint8_t *bss,
                          uint32_t bss_address, uint32_t bss_size, const uint8_t *initial_globals)
{
    later_state_test_shutdown();
    uint8_t *file = region_for(&loader, 0x823500u, size);
    uint8_t *zero = region_for(&loader, bss_address, bss_size);
    if (!file || !zero) return -1;
    memcpy(file, overlay, size);
    memcpy(zero, bss, bss_size);
    memcpy(state.globals, initial_globals, sizeof state.globals);
    return em_area01_state_bind_area(&state, memory, &loader, area);
}
uint8_t *later_state_test_view(uint32_t address, uint32_t size)
{ return em_area01_state_bytes(&state, address, size); }
int later_state_test_dispatch(uint8_t area, uint8_t sub) { return em_area01_state_001E7780(&state, area, sub); }
uint32_t later_state_test_fault(void) { return state.fault; }
