#include "game/em_area04_state.h"

#include <stddef.h>
#include <string.h>

#define OVERLAY_BASE 0x00823500u
#define OVERLAY_TEXT 0x30C0u

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(EmArea04State *s, uint32_t address)
{
    if (s && !s->fault) s->fault = address;
    return -1;
}

static int valid(EmArea04State *s)
{
    if (!s || s->fault || !s->memory || !s->globals) return 0;
    const uint8_t *h = s->memory(s->ctx, OVERLAY_BASE, 0x20);
    return h && !memcmp(h, "MWo3", 4) && word(h + 4) == 5 && word(h + 8) == OVERLAY_BASE &&
           word(h + 0xC) == OVERLAY_TEXT && word(h + 0x10) == EM_AREA04_STATE_DATA_SIZE &&
           word(h + 0x14) == EM_AREA04_STATE_BSS_SIZE && word(h + 0x18) == EM_AREA04_STATE_BSS &&
           word(h + 0x1C) == EM_AREA04_STATE_BSS;
}

static int inside(uint32_t address, uint32_t size, uint32_t base, uint32_t length)
{
    return size && address >= base && size <= length && address - base <= length - size;
}

uint8_t *em_area04_state_bytes(EmArea04State *s, uint32_t address, uint32_t size)
{
    if (!valid(s)) return NULL;
    if (inside(address, size, EM_AREA04_STATE_GLOBALS, EM_AREA04_STATE_GLOBAL_SIZE))
        return s->globals + address - EM_AREA04_STATE_GLOBALS;
    if (inside(address, size, EM_AREA04_STATE_DATA, EM_AREA04_STATE_DATA_SIZE) ||
        inside(address, size, EM_AREA04_STATE_BSS, EM_AREA04_STATE_BSS_SIZE))
        return s->memory(s->ctx, address, size);
    return NULL;
}

int em_area04_state_bind(EmArea04State *s, EmArea04StateMemory memory, void *ctx,
                         uint8_t globals[EM_AREA04_STATE_GLOBAL_SIZE])
{
    if (!s || s->fault) return -1;
    s->memory = memory;
    s->ctx = ctx;
    s->globals = globals;
    if (!valid(s)) return fail(s, OVERLAY_BASE);
    if (!em_area04_state_bytes(s, EM_AREA04_STATE_DATA, EM_AREA04_STATE_DATA_SIZE))
        return fail(s, EM_AREA04_STATE_DATA);
    if (!em_area04_state_bytes(s, EM_AREA04_STATE_BSS, EM_AREA04_STATE_BSS_SIZE))
        return fail(s, EM_AREA04_STATE_BSS);
    return 0;
}

void em_area04_state_detach(EmArea04State *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static int store(EmArea04State *s, uint32_t address, uint32_t value)
{
    uint8_t *p = em_area04_state_bytes(s, address, 4);
    if (!p) return fail(s, address);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
    return 0;
}

int em_area04_state_00823B10(EmArea04State *s)
{
    if (!s || s->fault) return -1;
    if (store(s, 0x00275C2Cu, 1) < 0 || store(s, 0x00275C28u, 0x20) < 0 ||
        store(s, 0x00275C20u, EM_AREA04_STATE_BSS) < 0 || store(s, 0x00275C24u, 0) < 0 ||
        store(s, 0x00275C1Cu, 0x00836980u) < 0)
        return -1;
    return 0;
}
