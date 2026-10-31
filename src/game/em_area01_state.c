#include "game/em_area01_state.h"
#include "game/em_area22_port.h"
#include "game/em_area06_port.h"

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

/* The middle of the route's overlays (AREA22, AREA06): the delivered MWo3
 * header (id, text, data and BSS as the disc's file states them), the data
 * after the 0x40-byte header and the text, and the BSS after the file. */
typedef struct {
    unsigned area;
    uint32_t id, text, data_size, bss, bss_size;
} MidOverlay;
static const MidOverlay k_mid[] = {
    {0x16u, 0x13u, 0xC0u, EM_AREA01_STATE_A22_DATA_SIZE, EM_AREA01_STATE_A22_BSS, EM_AREA01_STATE_A22_BSS_SIZE},
    {0x06u, 0x06u, 0x2AC0u, EM_AREA01_STATE_A06_DATA_SIZE, EM_AREA01_STATE_A06_BSS, EM_AREA01_STATE_A06_BSS_SIZE},
    {0x13u, 0x10u, 0x68C0u, EM_AREA01_STATE_A19_DATA_SIZE, EM_AREA01_STATE_A19_BSS, EM_AREA01_STATE_A19_BSS_SIZE},
};

static const MidOverlay *mid_overlay(unsigned area)
{
    for (unsigned i = 0; i < sizeof k_mid / sizeof k_mid[0]; ++i)
        if (k_mid[i].area == area) return &k_mid[i];
    return NULL;
}

static int mid_valid(EmArea01State *s, const MidOverlay *o)
{
    if (!s || s->fault || !s->memory || !o) return 0;
    const uint8_t *h = s->memory(s->ctx, OVERLAY_BASE, 0x20);
    return h && !memcmp(h, "MWo3", 4) && rd32(h + 4) == o->id && rd32(h + 8) == OVERLAY_BASE &&
           rd32(h + 0xC) == o->text && rd32(h + 0x10) == o->data_size && rd32(h + 0x14) == o->bss_size &&
           rd32(h + 0x18) == o->bss && rd32(h + 0x1C) == o->bss &&
           OVERLAY_BASE + 0x40u + o->text + o->data_size == o->bss;
}

int em_area01_state_supported(unsigned area)
{
    return area <= 2 || area == 4 || mid_overlay(area) != NULL;
}

uint8_t *em_area01_state_bytes(EmArea01State *s, uint32_t address, uint32_t size)
{
    const MidOverlay *mid = s ? mid_overlay(s->area) : NULL;
    if (mid) {
        if (!mid_valid(s, mid)) return NULL;
        if (inside(address, size, EM_AREA01_STATE_GLOBALS, EM_AREA01_STATE_GLOBAL_SIZE))
            return s->globals + address - EM_AREA01_STATE_GLOBALS;
        if (inside(address, size, OVERLAY_BASE + 0x40u + mid->text, mid->data_size) ||
            inside(address, size, mid->bss, mid->bss_size))
            return s->memory(s->ctx, address, size);
        return NULL;
    }
    if (s && !s->fault && s->area == 0)
        return em_area00_state_bytes(&s->area00, address, size);
    if (s && !s->fault && s->area == 2)
        return em_area02_state_bytes(&s->area02, address, size);
    if (s && !s->fault && s->area == 4)
        return em_area04_state_bytes(&s->area04, address, size);
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
    return em_area01_state_bind_area(s, memory, ctx, 1);
}

int em_area01_state_bind_area(EmArea01State *s, EmArea01StateMemory memory, void *ctx, unsigned area)
{
    if (!s || s->fault) return -1;
    if (!em_area01_state_supported(area)) return fail(s, OVERLAY_BASE);
    s->memory = memory;
    s->ctx = ctx;
    s->area = area;
    const MidOverlay *mid = mid_overlay(area);
    if (mid) {
        if (!mid_valid(s, mid)) return fail(s, OVERLAY_BASE);
        if (!em_area01_state_bytes(s, OVERLAY_BASE + 0x40u + mid->text, mid->data_size))
            return fail(s, OVERLAY_BASE + 0x40u + mid->text);
        if (!em_area01_state_bytes(s, mid->bss, mid->bss_size))
            return fail(s, mid->bss);
        return 0;
    }
    if (area == 0) {
        if (em_area00_state_bind(&s->area00, memory, ctx, s->globals) < 0)
            return fail(s, s->area00.fault);
        return 0;
    }
    if (area == 2) {
        if (em_area02_state_bind(&s->area02, memory, ctx, s->globals) < 0)
            return fail(s, s->area02.fault);
        return 0;
    }
    if (area == 4) {
        if (em_area04_state_bind(&s->area04, memory, ctx, s->globals) < 0)
            return fail(s, s->area04.fault);
        return 0;
    }
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
    if (s->area != 1) return fail(s, 0x00823A50u);
    STORE(0x00275C2Cu, 1);
    STORE(0x00275C28u, 0x20);
    STORE(0x00275C20u, EM_AREA01_STATE_BSS);
    STORE(0x00275C24u, 0);
    STORE(0x00275C18u, 0);
    STORE(0x00275C1Cu, 0x00836D80u);
    return 0;
}

/* AREA22's init 0x823580 (key 0x1600): em_area22_port_00823580, its one
 * translation (byte-matched C, test_area22_port_reference), over this
 * provider's view (AREA06's init uses the same view). */
static uint8_t *a22_view(void *ctx, uint32_t address, uint32_t size)
{
    return em_area01_state_bytes(ctx, address, size);
}

/* AREA06's init, runtime 0x824290 (keys 0x600 / 0x601):
 * em_area06_port_00824290, its one translation (byte-matched C,
 * func_overlay_AREA06_00824250.c; test_area06_port_reference), over this
 * provider's view: D_00275C2C = 1, C28 = 0x20, C20 = the overlay's BSS
 * 0x828F00, C24 = 0, C1C = 0x832F80. */
int em_area01_state_00824290(EmArea01State *s)
{
    if (!s || s->fault) return -1;
    if (s->area != 6) return fail(s, 0x00824290u);
    EmArea06PortHooks h;
    memset(&h, 0, sizeof h);
    h.ctx = s;
    h.bytes = a22_view;
    EmArea06PortFault f = {0, 0};
    if (em_area06_port_00824290(&h, &f) < 0) return fail(s, f.address ? f.address : 0x00824290u);
    return 0;
}

/* AREA19's init 0x8250C0 (byte-matched C): D_00275C2C = 3, D_00275C28 =
 * 0x20, D_00275C20 = its BSS's first record 0x82F880, D_00275C24 = 0,
 * D_00275C1C = 0x84D9C0 (test_area19_state_reference). */
int em_area01_state_008250C0(EmArea01State *s)
{
    if (!s || s->fault) return -1;
    if (s->area != 0x13) return fail(s, 0x008250C0u);
    STORE(0x00275C2Cu, 3);
    STORE(0x00275C28u, 0x20);
    STORE(0x00275C20u, EM_AREA01_STATE_A19_BSS);
    STORE(0x00275C24u, 0);
    STORE(0x00275C1Cu, 0x0084D9C0u);
    return 0;
}

int em_area01_state_001E7780(EmArea01State *s, uint8_t area, uint8_t sub)
{
    if (!s || s->fault) return -1;
    if (!em_area01_state_supported(area) || area != s->area) return fail(s, 0x001E7780u);
    STORE(0x00275C24u, 0);
    STORE(0x00275C28u, 0);
    STORE(0x00275C2Cu, 0);
    STORE(0x00275C18u, 0);
    STORE(0x00275C1Cu, 0);
    STORE(0x00275C20u, 0);
    if (area == 0 && sub <= 2) {
        if (em_area00_state_00824E00(&s->area00) < 0)
            return fail(s, s->area00.fault);
    } else if (area == 1 && sub <= 1) {
        if (em_area01_state_00823A50(s) < 0) return -1;
    } else if (area == 2 && sub <= 2) {
        if (em_area02_state_00823900(&s->area02) < 0)
            return fail(s, s->area02.fault);
    } else if (area == 4 && sub <= 1) {
        if (em_area04_state_00823B10(&s->area04) < 0)
            return fail(s, s->area04.fault);
    } else if (area == 0x16 && sub == 0) {
        EmArea22PortHooks h;
        memset(&h, 0, sizeof h);
        h.ctx = s;
        h.bytes = a22_view;
        EmArea22PortFault f = {0, 0};
        if (em_area22_port_00823580(&h, &f) < 0) return fail(s, f.address ? f.address : 0x00823580u);
    } else if (area == 6 && sub <= 1) {
        if (em_area01_state_00824290(s) < 0) return -1;
    } else if (area == 0x13 && sub <= 1) {
        if (em_area01_state_008250C0(s) < 0) return -1;
    }
    /* Original 001E7780 walks the initializer's canonical pointer/count,
     * rather than clearing either overlay's whole BSS on a rebuild. */
    uint32_t base = rd32(s->globals + 8);
    int32_t count = (int32_t)rd32(s->globals + 20);
    for (int32_t i = 0; i < count; ++i) {
        STORE(base + (uint32_t)i * 0xA060u + 0x54u, 0);
        STORE(base + (uint32_t)i * 0xA060u + 0x58u, 0);
    }
    return 0;
}
