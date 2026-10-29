#include "game/em_area02_state.h"
#include "game/em_area02_overlay.h"

#include <stddef.h>
#include <string.h>

#define OVERLAY_BASE 0x00823500u
#define OVERLAY_TEXT 0x2140u

static uint32_t word(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int fail(EmArea02State *s, uint32_t address)
{
    if (s && !s->fault) s->fault = address;
    return -1;
}

static int valid(EmArea02State *s)
{
    if (!s || s->fault || !s->memory || !s->globals) return 0;
    const uint8_t *h = s->memory(s->ctx, OVERLAY_BASE, 0x20);
    return h && !memcmp(h, "MWo3", 4) && word(h + 4) == 3 && word(h + 8) == OVERLAY_BASE &&
           word(h + 0xC) == OVERLAY_TEXT && word(h + 0x10) == EM_AREA02_STATE_DATA_SIZE &&
           word(h + 0x14) == EM_AREA02_STATE_BSS_SIZE && word(h + 0x18) == EM_AREA02_STATE_BSS &&
           word(h + 0x1C) == EM_AREA02_STATE_BSS;
}

static int inside(uint32_t address, uint32_t size, uint32_t base, uint32_t length)
{
    return size && address >= base && size <= length && address - base <= length - size;
}

uint8_t *em_area02_state_bytes(EmArea02State *s, uint32_t address, uint32_t size)
{
    if (!valid(s)) return NULL;
    if (inside(address, size, EM_AREA02_STATE_GLOBALS, EM_AREA02_STATE_GLOBAL_SIZE))
        return s->globals + address - EM_AREA02_STATE_GLOBALS;
    if (inside(address, size, EM_AREA02_STATE_DATA, EM_AREA02_STATE_DATA_SIZE) ||
        inside(address, size, EM_AREA02_STATE_BSS, EM_AREA02_STATE_BSS_SIZE))
        return s->memory(s->ctx, address, size);
    return NULL;
}

int em_area02_state_bind(EmArea02State *s, EmArea02StateMemory memory, void *ctx,
                         uint8_t globals[EM_AREA02_STATE_GLOBAL_SIZE])
{
    if (!s || s->fault) return -1;
    s->memory = memory;
    s->ctx = ctx;
    s->globals = globals;
    if (!valid(s)) return fail(s, OVERLAY_BASE);
    if (!em_area02_state_bytes(s, EM_AREA02_STATE_DATA, EM_AREA02_STATE_DATA_SIZE))
        return fail(s, EM_AREA02_STATE_DATA);
    if (!em_area02_state_bytes(s, EM_AREA02_STATE_BSS, EM_AREA02_STATE_BSS_SIZE))
        return fail(s, EM_AREA02_STATE_BSS);
    return 0;
}

void em_area02_state_detach(EmArea02State *s)
{
    if (s) memset(s, 0, sizeof *s);
}

static uint8_t *view(void *ctx, uint32_t address, uint32_t size)
{
    return em_area02_state_bytes(ctx, address, size);
}

int em_area02_state_00823900(EmArea02State *s)
{
    if (!s || s->fault) return -1;
    EmArea02OvlHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = s;
    h.bytes = view;
    EmArea02OvlFault f = {0, 0};
    if (em_area02_ovl_00823900(&h, &f) < 0) return fail(s, f.address ? f.address : 0x00823900u);
    return 0;
}
