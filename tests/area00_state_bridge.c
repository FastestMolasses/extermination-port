/* Test-only loader access. Production borrows its loader's allocations. */
#include "../src/game/em_module_loader.c"
#include "game/em_area00_state.h"

static EmModuleLoader loader;
static EmArea00State state;
static uint8_t globals[EM_AREA00_STATE_GLOBAL_SIZE];
static uint32_t stores[8][2], count;

void em_area00_state_store_trace(uint32_t address, uint32_t value)
{
    if (count < 8) {
        stores[count][0] = address;
        stores[count][1] = value;
    }
    ++count;
}

static uint8_t *memory(void *ctx, uint32_t address, uint32_t size)
{ return em_module_loader_memory_mutable(ctx, address, size); }

void area00_state_test_shutdown(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) free(loader.regions[i].bytes);
    memset(&loader, 0, sizeof loader);
    em_area00_state_detach(&state);
}

int area00_state_test_seed(const uint8_t *overlay, uint32_t size, const uint8_t *bss,
                           const uint8_t *initial_globals)
{
    area00_state_test_shutdown();
    uint8_t *file = region_for(&loader, 0x823500u, size);
    uint8_t *zero = region_for(&loader, EM_AREA00_STATE_BSS, EM_AREA00_STATE_BSS_SIZE + 32);
    if (!file || !zero) return -1;
    memcpy(file, overlay, size);
    memcpy(zero, bss, EM_AREA00_STATE_BSS_SIZE + 32);
    memcpy(globals, initial_globals, sizeof globals);
    count = 0;
    return em_area00_state_bind(&state, memory, &loader, globals);
}

uint8_t *area00_state_test_view(uint32_t address, uint32_t size)
{ return em_area00_state_bytes(&state, address, size); }
uint8_t *area00_state_test_loader(uint32_t address, uint32_t size)
{ return em_module_loader_memory_mutable(&loader, address, size); }
uint8_t *area00_state_test_globals(void) { return globals; }
int area00_state_test_init(void)
{
    count = 0;
    return em_area00_state_00824E00(&state);
}
uint32_t area00_state_test_fault(void) { return state.fault; }
uint32_t area00_state_test_stores(uint32_t *out)
{
    if (count <= 8) memcpy(out, stores, count * sizeof stores[0]);
    return count;
}
int area00_state_test_rebind(void)
{ return em_area00_state_bind(&state, memory, &loader, globals); }
void area00_state_test_drop_bss(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i)
        if (loader.regions[i].address == EM_AREA00_STATE_BSS) {
            free(loader.regions[i].bytes);
            memset(&loader.regions[i], 0, sizeof loader.regions[i]);
        }
}
int area00_state_test_move_bss(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) {
        Region *r = &loader.regions[i];
        if (r->address != EM_AREA00_STATE_BSS || !r->bytes) continue;
        uint8_t *next = malloc(r->size);
        if (!next) return -1;
        memcpy(next, r->bytes, r->size);
        free(r->bytes);
        r->bytes = next;
        return 0;
    }
    return -1;
}
void area00_state_test_shorten_data(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i)
        if (loader.regions[i].address == 0x823500u)
            loader.regions[i].size--;
}
