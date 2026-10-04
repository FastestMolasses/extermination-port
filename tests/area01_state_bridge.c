/* Test-only access to the actual loader allocations and its 002009E0.
 * Production owns no second overlay arena. */
#include "../src/game/em_module_loader.c"
#include "game/em_area01_state.h"

static EmModuleLoader loader;
static EmArea01State state;
static uint32_t stores[32][2], store_count;

void em_area01_state_store_trace(uint32_t address, uint32_t value)
{
    if (store_count < 32) {
        stores[store_count][0] = address;
        stores[store_count][1] = value;
    }
    ++store_count;
}

static uint8_t *memory(void *ctx, uint32_t address, uint32_t size)
{
    return em_module_loader_memory_mutable(ctx, address, size);
}

int area_state_test_seed(const uint8_t *overlay, uint32_t size, const uint8_t *bss,
                         const uint8_t *globals)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) free(loader.regions[i].bytes);
    memset(&loader, 0, sizeof loader);
    em_area01_state_detach(&state);
    uint8_t *file = region_for(&loader, 0x823500u, size);
    /* BSS and its guard start contiguous for the allocator-move test;
     * the oracle compares them independently if a loader operation later
     * represents the adjacent intervals as separate allocations. */
    uint8_t *zero = region_for(&loader, EM_AREA01_STATE_BSS, EM_AREA01_STATE_BSS_SIZE + 32);
    if (!file || !zero) return -1;
    memcpy(file, overlay, size);
    memcpy(zero, bss, EM_AREA01_STATE_BSS_SIZE + 32);
    memcpy(state.globals, globals, sizeof state.globals);
    store_count = 0;
    return em_area01_state_bind(&state, memory, &loader);
}

uint8_t *area_state_test_view(uint32_t address, uint32_t size)
{ return em_area01_state_bytes(&state, address, size); }

uint8_t *area_state_test_loader(uint32_t address, uint32_t size)
{ return em_module_loader_memory_mutable(&loader, address, size); }

int area_state_test_init(int direct, uint8_t area, uint8_t sub)
{
    store_count = 0;
    return direct ? em_area01_state_00823A50(&state) :
                    em_area01_state_001E7780(&state, area, sub);
}

int area_state_test_clear(void)
{ return w_overlay(&loader, 0x823500u, 0x9800u); }

uint32_t area_state_test_fault(void) { return state.fault; }
uint32_t area_state_test_stores(uint32_t *out)
{
    if (store_count <= 32) memcpy(out, stores, store_count * sizeof stores[0]);
    return store_count;
}

/* A later loader allocation can move. A state view must resolve the
 * loader's current allocation rather than retain the previous pointer. */
int area_state_test_move_bss(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) {
        Region *r = &loader.regions[i];
        if (r->address != EM_AREA01_STATE_BSS || !r->bytes) continue;
        uint8_t *next = malloc(r->size);
        if (!next) return -1;
        memcpy(next, r->bytes, r->size);
        free(r->bytes);
        r->bytes = next;
        return 0;
    }
    return -1;
}

void area_state_test_drop_bss(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i)
        if (loader.regions[i].address == EM_AREA01_STATE_BSS) {
            free(loader.regions[i].bytes);
            memset(&loader.regions[i], 0, sizeof loader.regions[i]);
        }
}

void area_state_test_shorten_data(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i)
        if (loader.regions[i].address == 0x823500u)
            loader.regions[i].size = 0x9800u - 1;
}

int area_state_test_rebind(void)
{ return em_area01_state_bind(&state, memory, &loader); }

void area_state_test_shutdown(void)
{
    for (unsigned i = 0; i < MAX_REGIONS; ++i) free(loader.regions[i].bytes);
    memset(&loader, 0, sizeof loader);
    em_area01_state_detach(&state);
}
