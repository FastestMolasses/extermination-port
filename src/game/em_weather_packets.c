/* em_weather_packets.c - 001CFAE0, 001CFFE0 and 001E55F0's close of the
 * weather's channel-3 list (em_weather_packets.h). */
#include "game/em_weather_packets.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int fail(uint32_t *fault, uint32_t address)
{
    if (fault) *fault = address;
    return -1;
}

void em_weather_packets_001CFAE0(EmWeatherDrawState *dst, int32_t a1, const uint8_t src[64],
                                 uint32_t f12, uint32_t f13, uint32_t f14, uint32_t f15,
                                 uint32_t d275670)
{
    dst->w44 = f12;
    dst->w4C = f13;
    dst->w48 = f14;
    dst->w50 = f15;
    dst->w54 = 0;
    dst->m40 = d275670 + ((uint32_t)a1 << 6) + 0x2240u;   /* 001CD370(a1) */
    memcpy(dst->q, src, 64);
}

/* 001CFFE0's two jump tables: kind 1, and kinds 2 / 3 / 4, by variant: the
 * program packet it CALLs and the D_00251260 row (its GIF tag row). */
static const uint32_t k_table_1[7] = {
    0x00230800u, 0x00231770u, 0x00232540u, 0x00233800u, 0x00230800u, 0x00231770u, 0x0023D930u
};
static const uint8_t k_row_1[7] = { 0, 2, 4, 2, 7, 6, 2 };
static const uint32_t k_table_234[7] = {
    0x00230800u, 0x00231770u, 0x00232540u, 0x00233800u, 0x00230800u, 0x00231770u, 0x0023D930u
};
static const uint8_t k_row_234[7] = { 1, 3, 5, 3, 7, 6, 3 };

/* 001CB9B0(kind): D_00275674 + 0x6A0 + 0x80 * kind for 0..4. */
static int preset(uint32_t d275674, int32_t kind, uint32_t *out)
{
    if (kind < 0 || kind > 4) return -1;   /* the original returns an unset v0 */
    *out = d275674 + 0x6A0u + 0x80u * (uint32_t)kind;
    return 0;
}

/* The channel cursor's word, and `size` writable bytes at the cursor; the
 * cursor advances by `size`. */
static uint8_t *open_packet(const EmWeatherMem *m, uint32_t cursor_at, uint32_t size, uint32_t *fault)
{
    uint8_t *cw = m->write(m->ctx, cursor_at, 4);
    if (!cw) {
        fail(fault, cursor_at);
        return NULL;
    }
    const uint32_t cursor = rd32(cw);
    uint8_t *q = m->write(m->ctx, cursor, size);
    if (!q) {
        fail(fault, cursor);
        return NULL;
    }
    wr32(cw, cursor + size);
    return q;
}

/* A tag's written parts: the QWC halfword, the ID byte and the address. */
static void tag(uint8_t *q, uint32_t qwc, uint8_t id, uint32_t address)
{
    q[3] = id;
    wr32(q + 4, address);
    q[0] = (uint8_t)qwc;
    q[1] = (uint8_t)(qwc >> 8);
}

static int copy_in(const EmWeatherMem *m, uint8_t *dst, uint32_t address, uint32_t size, uint32_t *fault)
{
    const uint8_t *b = m->read(m->ctx, address, size);
    if (!b) return fail(fault, address);
    memcpy(dst, b, size);
    return 0;
}

int em_weather_packets_001CFFE0(const EmWeatherMem *m, int32_t slot, uint32_t variant,
                                const uint8_t obj[0x90], const EmWeatherDrawState *src,
                                uint32_t *fault)
{
    if (!m || !m->read || !m->write || !obj || !src) return fail(fault, 0x001CFFE0u);
    const int32_t kind = (int32_t)rd32(obj + 0x8C);
    uint32_t tbl;
    unsigned row;
    if (variant > 6u) return fail(fault, 0x001CFFE0u);
    if (kind == 1) {
        tbl = k_table_1[variant];
        row = k_row_1[variant];
    } else if (kind >= 2 && kind <= 4) {
        tbl = k_table_234[variant];
        row = k_row_234[variant];
    } else {
        return fail(fault, 0x001CFFE0u);                    /* s0 / s1 never set */
    }
    uint32_t ref;
    if (preset(m->d275674, kind, &ref) < 0) return fail(fault, 0x001CB9B0u);
    const uint32_t cursor_at = m->d275670 + 0x10u + 4u * (uint32_t)slot;
    uint8_t *q;

    /* Packet 1: REF of the blend preset, 8 qwords. */
    if (!(q = open_packet(m, cursor_at, 0x10u, fault))) return -1;
    tag(q, 8, 0x30, ref);
    /* Packet 2: CALL of the program packet. */
    if (!(q = open_packet(m, cursor_at, 0x10u, fault))) return -1;
    tag(q, 0, 0x50, tbl);

    /* Packet 3: CNT 0x10: STCYCL 1/1, UNPACK V4-32 15 rows to 0x6E. */
    if (!(q = open_packet(m, cursor_at, 0x110u, fault))) return -1;
    tag(q, 0x10, 0x10, 0);
    memset(q + 0x10, 0, 16);
    wr32(q + 0x18, 0x01000101u);
    wr32(q + 0x1C, 0x6C0F006Eu);
    if (copy_in(m, q + 0x20, 0x70003A40u, 0x40, fault) < 0) return -1;     /* P */
    if (copy_in(m, q + 0x60, src->m40, 0x40, fault) < 0) return -1;        /* the clip projection */
    if (copy_in(m, q + 0xA0, 0x70003AC0u, 0x40, fault) < 0) return -1;     /* K */
    if (copy_in(m, q + 0xE0, m->d275670 + 0xA0u, 0x10, fault) < 0) return -1;  /* the fog */
    wr32(q + 0xF0, 0);
    wr32(q + 0xF4, 0);
    wr32(q + 0xF8, src->w54);
    wr32(q + 0xFC, 0);
    if (copy_in(m, q + 0x100, 0x00251260u + 0x10u * row, 0x10, fault) < 0) return -1;

    /* Packet 4: CNT 0xA: UNPACK 9 rows to 0x50, the descriptor. */
    if (!(q = open_packet(m, cursor_at, 0xB0u, fault))) return -1;
    tag(q, 0xA, 0x10, 0);
    memset(q + 0x10, 0, 16);
    wr32(q + 0x1C, 0x6C090050u);
    memcpy(q + 0x20, obj, 0x90);

    /* Packet 5: CNT 7: UNPACK 5 rows to 0x59, the parameters and the
     * matrix; MSCAL 0, FLUSH. */
    if (!(q = open_packet(m, cursor_at, 0x80u, fault))) return -1;
    tag(q, 7, 0x10, 0);
    memset(q + 0x10, 0, 16);
    wr32(q + 0x1C, 0x6C050059u);
    wr32(q + 0x20, src->w44);
    wr32(q + 0x24, src->w48);
    wr32(q + 0x28, src->w50);
    wr32(q + 0x2C, src->w4C);
    memcpy(q + 0x30, src->q, 64);
    memset(q + 0x70, 0, 16);
    wr32(q + 0x70, 0x14000000u);
    wr32(q + 0x74, 0x11000000u);
    return 0;
}

int em_weather_packets_tile(const EmWeatherMem *m, const uint8_t descriptor[0x90], const uint32_t vu59[4],
                            const uint8_t matrix[64], uint32_t *fault)
{
    if (!m || !descriptor || !vu59 || !matrix) return fail(fault, 0x001E67C0u);
    EmWeatherDrawState state;
    /* 001CFAE0 packs f12 / f14 / f15 / f13 as VU59's four words. */
    em_weather_packets_001CFAE0(&state, 0, matrix, vu59[0], vu59[3], vu59[1], vu59[2], m->d275670);
    return em_weather_packets_001CFFE0(m, 3, 3, descriptor, &state, fault);
}

int em_weather_packets_close(const EmWeatherMem *m, uint32_t start, uint32_t *pending, uint32_t *fault)
{
    if (!m || !m->write || !pending) return fail(fault, 0x001E55F0u);
    uint8_t *q = open_packet(m, m->d275670 + 0x1Cu, 0x10u, fault);
    if (!q) return -1;
    tag(q, 0, 0x60, 0);
    *pending = start;
    return 0;
}
