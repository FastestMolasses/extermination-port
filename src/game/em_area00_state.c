#include "game/em_area00_state.h"

#include <stddef.h>
#include <string.h>

#define OVERLAY_BASE 0x00823500u
#define OVERLAY_FILE_SIZE 0x9F80u

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(EmArea00State *s, uint32_t address)
{
    if (s && !s->fault) s->fault = address;
    return -1;
}

static int valid(EmArea00State *s)
{
    if (!s || s->fault || !s->memory || !s->globals) return 0;
    const uint8_t *h = s->memory(s->ctx, OVERLAY_BASE, 0x20);
    return h && !memcmp(h, "MWo3", 4) && word(h + 4) == 1 && word(h + 8) == OVERLAY_BASE &&
           word(h + 0xC) == 0x3A40u && word(h + 0x10) == EM_AREA00_STATE_DATA_SIZE &&
           0x40u + word(h + 0xC) + word(h + 0x10) == OVERLAY_FILE_SIZE &&
           word(h + 0x14) == EM_AREA00_STATE_BSS_SIZE && word(h + 0x18) == EM_AREA00_STATE_BSS &&
           word(h + 0x1C) == EM_AREA00_STATE_BSS;
}

static int inside(uint32_t address, uint32_t size, uint32_t base, uint32_t length)
{
    return size && address >= base && size <= length && address - base <= length - size;
}

uint8_t *em_area00_state_bytes(EmArea00State *s, uint32_t address, uint32_t size)
{
    if (!valid(s)) return NULL;
    if (inside(address, size, EM_AREA00_STATE_GLOBALS, EM_AREA00_STATE_GLOBAL_SIZE))
        return s->globals + address - EM_AREA00_STATE_GLOBALS;
    if (inside(address, size, EM_AREA00_STATE_DATA, EM_AREA00_STATE_DATA_SIZE) ||
        inside(address, size, EM_AREA00_STATE_BSS, EM_AREA00_STATE_BSS_SIZE))
        return s->memory(s->ctx, address, size);
    return NULL;
}

int em_area00_state_bind(EmArea00State *s, EmArea00StateMemory memory, void *ctx,
                        uint8_t globals[EM_AREA00_STATE_GLOBAL_SIZE])
{
    if (!s || s->fault) return -1;
    s->memory = memory;
    s->ctx = ctx;
    s->globals = globals;
    if (!valid(s)) return fail(s, OVERLAY_BASE);
    if (!em_area00_state_bytes(s, EM_AREA00_STATE_DATA, EM_AREA00_STATE_DATA_SIZE))
        return fail(s, EM_AREA00_STATE_DATA);
    if (!em_area00_state_bytes(s, EM_AREA00_STATE_BSS, EM_AREA00_STATE_BSS_SIZE))
        return fail(s, EM_AREA00_STATE_BSS);
    return 0;
}

void em_area00_state_detach(EmArea00State *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static int store(EmArea00State *s, uint32_t address, uint32_t value)
{
    uint8_t *p = em_area00_state_bytes(s, address, 4);
    if (!p) return fail(s, address);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
#ifdef EM_AREA00_STATE_STORE_TRACE
    extern void em_area00_state_store_trace(uint32_t, uint32_t);
    em_area00_state_store_trace(address, value);
#endif
    return 0;
}

int em_area00_state_00824E00(EmArea00State *s)
{
    if (!s || s->fault) return -1;
    /* Byte-matched decomp func_overlay_AREA00_00824DC0; the runtime
     * address includes the overlay header. Store order is original. */
    if (store(s, 0x00275C2Cu, 2) < 0 || store(s, 0x00275C28u, 0x20) < 0 ||
        store(s, 0x00275C24u, 0x20) < 0 || store(s, 0x00275C20u, EM_AREA00_STATE_BSS) < 0 ||
        store(s, 0x00275C1Cu, 0x00841540u) < 0 || store(s, 0x00275C18u, 0x00982140u) < 0)
        return -1;
    return 0;
}
