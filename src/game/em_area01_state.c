#include "game/em_area01_state.h"

#include <stddef.h>
#include <string.h>

#define OVERLAY_BASE 0x00823500u
#define OVERLAY_FILE_SIZE 0x9800u

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(EmArea01State *s, uint32_t address)
{
    if (s && !s->fault) s->fault = address;
    return -1;
}

static int valid(EmArea01State *s)
{
    if (!s || s->fault || !s->memory) return 0;
    const uint8_t *h = s->memory(s->ctx, OVERLAY_BASE, 0x20);
    /* The actual MWo3 header: loaded whole at base, file = header + text
     * + data; 002009E0 clears header[5] bytes immediately after that file.
     * Both original link boundary words identify the BSS start. */
    return h && !memcmp(h, "MWo3", 4) && rd32(h + 4) == 2 &&
           rd32(h + 8) == OVERLAY_BASE && rd32(h + 0xC) == 0x54C0 &&
           rd32(h + 0x10) == EM_AREA01_STATE_DATA_SIZE &&
           0x40u + rd32(h + 0xC) + rd32(h + 0x10) == OVERLAY_FILE_SIZE &&
           rd32(h + 0x14) == EM_AREA01_STATE_BSS_SIZE &&
           rd32(h + 0x18) == EM_AREA01_STATE_BSS &&
           rd32(h + 0x1C) == EM_AREA01_STATE_BSS;
}

static int inside(uint32_t address, uint32_t size, uint32_t base, uint32_t length)
{
    return size && address >= base && size <= length && address - base <= length - size;
}

uint8_t *em_area01_state_bytes(EmArea01State *s, uint32_t address, uint32_t size)
{
    if (!valid(s)) return NULL;
    if (inside(address, size, EM_AREA01_STATE_GLOBALS, EM_AREA01_STATE_GLOBAL_SIZE))
        return s->globals + address - EM_AREA01_STATE_GLOBALS;
    if (inside(address, size, EM_AREA01_STATE_DATA, EM_AREA01_STATE_DATA_SIZE) ||
        inside(address, size, EM_AREA01_STATE_BSS, EM_AREA01_STATE_BSS_SIZE))
        return s->memory(s->ctx, address, size);
    return NULL;
}

int em_area01_state_bind(EmArea01State *s, EmArea01StateMemory memory, void *ctx)
{
    if (!s || s->fault) return -1;
    s->memory = memory;
    s->ctx = ctx;
    if (!valid(s)) return fail(s, OVERLAY_BASE);
    if (!em_area01_state_bytes(s, EM_AREA01_STATE_DATA, EM_AREA01_STATE_DATA_SIZE))
        return fail(s, EM_AREA01_STATE_DATA);
    if (!em_area01_state_bytes(s, EM_AREA01_STATE_BSS, EM_AREA01_STATE_BSS_SIZE))
        return fail(s, EM_AREA01_STATE_BSS);
    return 0;
}

void em_area01_state_detach(EmArea01State *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static int store(EmArea01State *s, uint32_t address, uint32_t value)
{
    uint8_t *p = em_area01_state_bytes(s, address, 4);
    if (!p) return fail(s, address);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
#ifdef EM_AREA01_STATE_STORE_TRACE
    extern void em_area01_state_store_trace(uint32_t address, uint32_t value);
    em_area01_state_store_trace(address, value);
#endif
    return 0;
}

#define STORE(address, value) do { if (store(s, address, value) < 0) return -1; } while (0)

int em_area01_state_00823A50(EmArea01State *s)
{
    if (!s || s->fault) return -1;
    STORE(0x00275C2Cu, 1);
    STORE(0x00275C28u, 0x20);
    STORE(0x00275C20u, EM_AREA01_STATE_BSS);
    STORE(0x00275C24u, 0);
    STORE(0x00275C18u, 0);
    STORE(0x00275C1Cu, 0x00836D80u);
    return 0;
}

int em_area01_state_001E7780(EmArea01State *s, uint8_t area, uint8_t sub)
{
    if (!s || s->fault) return -1;
    if (area != 1) return fail(s, 0x001E7780u);
    STORE(0x00275C24u, 0);
    STORE(0x00275C28u, 0);
    STORE(0x00275C2Cu, 0);
    STORE(0x00275C18u, 0);
    STORE(0x00275C1Cu, 0);
    STORE(0x00275C20u, 0);
    if (sub <= 1) {
        if (em_area01_state_00823A50(s) < 0) return -1;
        /* AREA01's initializer sets one record. Other areas' dispatch
         * targets and record counts belong to their own bindings. */
        STORE(EM_AREA01_STATE_BSS + 0x54u, 0);
        STORE(EM_AREA01_STATE_BSS + 0x58u, 0);
    }
    return 0;
}
