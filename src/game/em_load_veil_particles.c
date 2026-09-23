/* Area-load veil particles and their draw packets. See
 * em_load_veil_particles.h and docs/LOAD_VEIL_PARTICLES.md. */
#include "game/em_load_veil_particles.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

typedef uint32_t u32;
typedef uint64_t u64;
typedef EmLoadVeilParticles S;

/* binary32 constants the originals load (lui/ori immediates). */
#define F_ZERO      0x00000000u
#define F_HALF      0x3F000000u /* 0.5 */
#define F_ONE       0x3F800000u /* 1.0 */
#define F_TWO       0x40000000u /* 2.0 */
#define F_TENTH     0x3DCCCCCDu /* 0.1 */
#define F_FIFTH     0x3E4CCCCDu /* 0.2 */
#define F_PHASE_STEP 0x3BE56042u /* 0.007 */
#define F_255       0x437F0000u
#define F_512       0x44000000u
#define F_1280      0x44A00000u
#define F_65536     0x47800000u
#define F_15        0x41700000u
#define F_COLUMN    0x44088889u /* 8192 / 15: the strip's GS X step */
#define F_LENS      0xBEE66666u /* -0.45: 0021B1B0's f12 for both 001DFA40 passes */

/* ------------------------------------------------------------------ */
/* Fault latch, little-endian byte access, 32-bit arithmetic shift.    */
/* ------------------------------------------------------------------ */

static int latched(const S *s) { return s->fault.code != EM_LVP_FAULT_NONE; }

static int fault(S *s, u32 address, int32_t code)
{
    if (!latched(s)) {
        s->fault.address = address;
        s->fault.code = code;
    }
    return -1;
}

static void put16(uint8_t *p, u32 v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void put64(uint8_t *p, u64 v) { put32(p, (u32)v); put32(p + 4, (u32)(v >> 32)); }
static u32 get16(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8; }
static u32 get32(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u64 get64(const uint8_t *p) { return (u64)get32(p) | (u64)get32(p + 4) << 32; }

/* The original's sra on a 32-bit register. */
static int32_t sra32(int32_t x, unsigned n) { return x < 0 ? ~(~x >> n) : x >> n; }
/* sll/sra by 16: the halfword sign extension of the SDK's short arguments. */
static int32_t sext16(u32 x) { return (int32_t)(int16_t)(uint16_t)(x & 0xFFFFu); }
/* A sign-extended 32-bit register value as the 64-bit register image. */
static u64 sx(int32_t x) { return (u64)(int64_t)x; }

/* ------------------------------------------------------------------ */
/* Channel cursor and packet window.                                  */
/* ------------------------------------------------------------------ */

/* The cursor word D_00275670 + 0x10 + 4 * chan. */
static u32 *cursor_of(S *s, int32_t chan, u32 fn)
{
    if (!s->world.cursor) {
        fault(s, 0x00275670u, EM_LVP_FAULT_NULL_WORKER);
        return NULL;
    }
    if (chan < 0 || (u32)chan >= s->world.cursor_count) {
        fault(s, fn, EM_LVP_FAULT_BAD_INDEX);
        return NULL;
    }
    return &s->world.cursor[chan];
}

/* Host bytes for original addresses [address, address + size). */
static uint8_t *window(S *s, u32 address, u32 size, u32 align, u32 fn)
{
    const EmLoadVeilParticlesWorld *w = &s->world;
    u32 at = address - w->packet_address;
    if (!w->packet) {
        fault(s, fn, EM_LVP_FAULT_NULL_WORKER);
        return NULL;
    }
    if ((address & (align - 1u)) || address < w->packet_address || at > w->packet_size ||
        size > w->packet_size - at) {
        fault(s, fn, EM_LVP_FAULT_BAD_INDEX);
        return NULL;
    }
    return w->packet + at;
}

/* The packet at the channel cursor: header byte +3, word +4 = 0 and the
 * halfword quadword count +0 (the DMA tag the builders stamp first), then
 * the cursor advances by `size`. Byte +2 and +8..+0xF are not written. */
static uint8_t *open_packet(S *s, int32_t chan, u32 size, uint8_t id, u32 qwc, u32 fn, u32 *at)
{
    u32 *c = cursor_of(s, chan, fn);
    uint8_t *p;
    if (!c)
        return NULL;
    p = window(s, *c, size, 16u, fn);
    if (!p)
        return NULL;
    p[3] = id;
    put32(p + 4, 0);
    put16(p, qwc);
    *at = *c;
    *c = *c + size;
    return p;
}

static int view_d00275674(S *s, u32 *value)
{
    if (!s->world.d00275674)
        return fault(s, 0x00275674u, EM_LVP_FAULT_NULL_WORKER);
    *value = *s->world.d00275674;
    return 0;
}

/* A REF tag (byte +3 = 0x30) to *D_00275674 + offset, qwc quadwords. */
static int ref_tag(S *s, int32_t chan, u32 offset, u32 qwc, u32 fn)
{
    u32 base, at;
    uint8_t *p;
    u32 *c;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, fn);
    if (!c || !window(s, *c, 0x10u, 16u, fn) || view_d00275674(s, &base) < 0)
        return -1;
    p = open_packet(s, chan, 0x10u, 0x30, qwc, fn, &at);
    put32(p + 4, base + offset);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Workers.                                                           */
/* ------------------------------------------------------------------ */

static int fabs_w(S *s, u32 x, u32 *out)
{
    if (!s->workers.w_0011DF78)
        return fault(s, 0x0011DF78u, EM_LVP_FAULT_NULL_WORKER);
    if (s->workers.w_0011DF78(s->workers.ctx, x, out) < 0)
        return fault(s, 0x0011DF78u, EM_LVP_FAULT_WORKER_FAILED);
    return 0;
}

static int to_int(S *s, u32 x, int32_t *out)
{
    if (!s->workers.w_001281C0)
        return fault(s, 0x001281C0u, EM_LVP_FAULT_NULL_WORKER);
    if (s->workers.w_001281C0(s->workers.ctx, x, out) < 0)
        return fault(s, 0x001281C0u, EM_LVP_FAULT_WORKER_FAILED);
    return 0;
}

/* ------------------------------------------------------------------ */
/* REF tag builders (byte-matched C).                                 */
/* ------------------------------------------------------------------ */

/* 001D1F80(chan, a1, a2): REF to +0xBA0 + a1 * 1440 + a2 * 144, 9 qwords. */
int em_load_veil_particles_001D1F80(S *s, int32_t chan, int32_t a1, int32_t a2)
{
    return ref_tag(s, chan, (u32)a1 * 1440u + (u32)a2 * 144u + 0xBA0u, 9u, 0x001D1F80u);
}

/* 001D1FF0(chan, a1): REF to +0x4A0 + (a1 << 6), 4 qwords. */
int em_load_veil_particles_001D1FF0(S *s, int32_t chan, int32_t a1)
{
    return ref_tag(s, chan, ((u32)a1 << 6) + 0x4A0u, 4u, 0x001D1FF0u);
}

/* 001D2040(chan, a1): REF to +0x5A0 + (a1 << 6), 4 qwords. */
int em_load_veil_particles_001D2040(S *s, int32_t chan, int32_t a1)
{
    return ref_tag(s, chan, ((u32)a1 << 6) + 0x5A0u, 4u, 0x001D2040u);
}

/* 001D1F20(chan): REF to +0x20 + *(ctx + 0x9C) * 400, 0x19 qwords. */
int em_load_veil_particles_001D1F20(S *s, int32_t chan)
{
    if (latched(s))
        return -1;
    if (!s->world.ctx_9C)
        return fault(s, 0x00275670u, EM_LVP_FAULT_NULL_WORKER);
    return ref_tag(s, chan, *s->world.ctx_9C * 400u + 0x20u, 0x19u, 0x001D1F20u);
}

/* ------------------------------------------------------------------ */
/* 001D63B0: one line segment. 7 qwords: DMA CNT (qwc 6), a DIRECT word  */
/* 0x50000005 at +0x1C, the GIF tag 0x4024C000_00008001 (+0x20) with     */
/* registers 0x4141 (+0x28), then a2 (4 words), a1 (3 words + 0), t0 (4  */
/* words), a3 (3 words + 0). Returns cursor + 0x10.                      */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001D63B0(S *s, int32_t chan, const uint32_t a1[3], const uint32_t a2[4],
                                    const uint32_t a3[3], const uint32_t t0[4], uint32_t *result)
{
    u32 at, *c;
    uint8_t *p;
    int i;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001D63B0u);
    if (!c || !window(s, *c, 0x70u, 16u, 0x001D63B0u))
        return -1;
    if (!a1 || !a2 || !a3 || !t0)
        return fault(s, 0x001D63B0u, EM_LVP_FAULT_NULL_WORKER);
    p = open_packet(s, chan, 0x70u, 0x10, 6u, 0x001D63B0u, &at);
    memset(p + 0x10, 0, 16);
    put32(p + 0x1C, 0x50000005u);
    put64(p + 0x20, (u64)0x4024C000u << 32 | 0x8001u);
    put64(p + 0x28, 0x4141u);
    for (i = 0; i < 4; ++i)
        put32(p + 0x30 + 4 * i, a2[i]);
    for (i = 0; i < 3; ++i)
        put32(p + 0x40 + 4 * i, a1[i]);
    put32(p + 0x4C, 0);
    for (i = 0; i < 4; ++i)
        put32(p + 0x50 + 4 * i, t0[i]);
    for (i = 0; i < 3; ++i)
        put32(p + 0x60 + 4 * i, a3[i]);
    put32(p + 0x6C, 0);
    if (result)
        *result = at + 0x10u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001D7080(chan, a1, f12): A+D RGBAQ. 4 qwords: DMA CNT (qwc 3), DIRECT  */
/* 0x50000002, GIF tag 0x10000000_00008001, registers 0xE, then the data */
/* word a1, the Q word f12, and the register number 1.                  */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001D7080(S *s, int32_t chan, uint32_t a1, uint32_t f12)
{
    u32 at, *c;
    uint8_t *p;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001D7080u);
    if (!c || !window(s, *c, 0x40u, 16u, 0x001D7080u))
        return -1;
    p = open_packet(s, chan, 0x40u, 0x10, 3u, 0x001D7080u, &at);
    memset(p + 0x10, 0, 16);
    put32(p + 0x1C, 0x50000002u);
    put64(p + 0x20, (u64)0x10000000u << 32 | 0x8001u);
    put64(p + 0x28, 0xEu);
    put32(p + 0x30, a1);
    put32(p + 0x34, f12);
    put64(p + 0x38, 1u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001D6BA0(chan, a1, a2, a3, t0, t1): A+D TEX0_1. 5 qwords: DMA CNT     */
/* (qwc 4), DIRECT 0x50000003, GIF tag 0x10000000_00008002, registers    */
/* 0xE, then (0, 0x3F) and the packed TEX0 value with register 6.        */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001D6BA0(S *s, int32_t chan, int32_t a1, int32_t a2, int32_t a3,
                                    int32_t t0, int32_t t1, uint32_t *result)
{
    u32 at, *c;
    uint8_t *p;
    u64 tex0;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001D6BA0u);
    if (!c || !window(s, *c, 0x50u, 16u, 0x001D6BA0u))
        return -1;
    /* Each field is the sign-extended 32-bit register, shifted as a dword. */
    tex0 = sx(sra32(a1, 8))                                          /* TBP0 */
         | sx(sra32((int32_t)(1u << ((u32)a2 & 31u)), 6)) << 14      /* TBW */
         | sx(a2) << 26 | sx(a3) << 30 | sx(t1) << 34 | sx(t0) << 35;
    p = open_packet(s, chan, 0x50u, 0x10, 4u, 0x001D6BA0u, &at);
    memset(p + 0x10, 0, 16);
    put32(p + 0x1C, 0x50000003u);
    put64(p + 0x20, (u64)0x10000000u << 32 | 0x8002u);
    put64(p + 0x28, 0xEu);
    put64(p + 0x30, 0);
    put64(p + 0x38, 0x3Fu);
    put64(p + 0x40, tex0);
    put64(p + 0x48, 6u);
    if (result)
        *result = at + 0x10u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 00100610(psm, w, h) with 00100268 = &D_00241010.                      */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_00100610(S *s, int32_t psm, int32_t w, int32_t h, int32_t *result)
{
    int32_t sw = sext16((u32)w), sp = sext16((u32)psm), sh = sext16((u32)h);
    int32_t across, down, v, product;
    u64 mode;
    if (latched(s))
        return -1;
    if (!s->world.d00241010)
        return fault(s, 0x00241010u, EM_LVP_FAULT_NULL_WORKER);
    v = sw + 0x3F;                                           /* (w + 63) / 64, signed */
    across = sra32(v > -1 ? v : sw + 0x7E, 6);
    if (sp & 2) {
        v = sh + 0x3F;
        down = sra32(v > -1 ? v : sh + 0x7E, 6);
    } else {
        v = sh + 0x1F;
        down = sra32(v > -1 ? v : sh + 0x3E, 5);
    }
    mode = get64(s->world.d00241010) & UINT64_C(0x0000FFFF0000FFFF);
    product = (int32_t)((u32)across * (u32)down);
    if (result)
        *result = mode == 1u ? sext16((u32)product) : sext16((u32)product << 1);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001006D8(env, psm, w, h, t0, t1): the SDK draw-environment fill. Every */
/* argument is a halfword (sign-extended). Register numbers at +0x08,    */
/* +0x18, +0x28 ... +0x78 are 0x4C, 0x4E, 0x18, 0x40, 0x1A, 0x46, 0x45,  */
/* 0x47. +0x40, +0x50 and +0x60 are read back and only bit 0 changes.    */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001006D8(S *s, uint32_t env, int32_t psm, int32_t w, int32_t h,
                                    int32_t t0, int32_t t1, int32_t *result)
{
    int32_t sw = sext16((u32)w), sp = sext16((u32)psm), sh = sext16((u32)h);
    int32_t s5 = sext16((u32)t0), s3 = sext16((u32)t1), zbuf;
    uint8_t *e;
    u64 frame, z, offset, scissor, v;
    if (latched(s))
        return -1;
    e = window(s, env, 0x80u, 8u, 0x001006D8u);
    if (!e)
        return -1;
    if (!s->world.d00241010)
        return fault(s, 0x00241010u, EM_LVP_FAULT_NULL_WORKER);
    frame = (u64)((u32)sra32(sw + 0x3F, 6) & 0x3Fu) << 16 | (u64)((u32)sp & 0xFu) << 24;
    put64(e + 0x08, 0x4Cu);
    put64(e + 0x00, frame);
    put64(e + 0x18, 0x4Eu);
    if (em_load_veil_particles_00100610(s, sp, sw, sh, &zbuf) < 0)
        return -1;
    z = sx(sext16((u32)zbuf)) | (u64)((u32)s3 & 0xFu) << 24;
    if (s5 == 0)
        z |= (u64)0x8000u << 17;                              /* ZMSK */
    put64(e + 0x10, z);
    offset = (u64)(0x800 - (int64_t)sext16((u32)sra32(sh, 1))) << 36
           | (u64)(0x800 - (int64_t)sext16((u32)sra32(sw, 1))) << 4;
    scissor = sx((int32_t)((u32)sw - 1u)) << 16 | sx((int32_t)((u32)sh - 1u)) << 48;
    put64(e + 0x28, 0x18u);
    put64(e + 0x20, offset);
    put64(e + 0x38, 0x40u);
    put64(e + 0x30, scissor);
    put64(e + 0x48, 0x1Au);
    put64(e + 0x40, get64(e + 0x40) | 1u);
    put64(e + 0x58, 0x46u);
    put64(e + 0x50, get64(e + 0x50) | 1u);
    put64(e + 0x68, 0x45u);
    v = get64(e + 0x60);
    put64(e + 0x60, (sp & 2) ? v | 1u : v & ~(u64)1u);
    put64(e + 0x78, 0x47u);
    put64(e + 0x70, s5 != 0 ? (u64)((u32)s5 & 3u) << 17 | 0x10000u : 0x30000u);
    if (result)
        *result = 8;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001D6E60(chan, a1, a2, a3): 11 qwords: DMA CNT (qwc 0xA), the words   */
/* 0, 0, 0x11000000, DIRECT 0x50000009, GIF tag 0x10000000_00008008,     */
/* registers 0xE, then 001006D8(+0x30, 0, (short)(1 << a2),              */
/* (short)(1 << a3), 0, 2) and the FRAME halfword's low 9 bits =         */
/* (a1 >> 13) & 0x1FF. Returns cursor + 0x10.                            */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001D6E60(S *s, int32_t chan, int32_t a1, int32_t a2, int32_t a3,
                                    uint32_t *result)
{
    u32 at, *c;
    uint8_t *p;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001D6E60u);
    if (!c || !window(s, *c, 0xB0u, 16u, 0x001D6E60u))
        return -1;
    if (!s->world.d00241010)
        return fault(s, 0x00241010u, EM_LVP_FAULT_NULL_WORKER);
    p = open_packet(s, chan, 0xB0u, 0x10, 0xAu, 0x001D6E60u, &at);
    put32(p + 0x10, 0);
    put32(p + 0x14, 0);
    put32(p + 0x18, 0x11000000u);
    put32(p + 0x1C, 0x50000009u);
    put64(p + 0x20, (u64)0x10000000u << 32 | 0x8008u);
    put64(p + 0x28, 0xEu);
    if (em_load_veil_particles_001006D8(s, at + 0x30u, 0, sext16(1u << ((u32)a2 & 31u)),
                                        sext16(1u << ((u32)a3 & 31u)), 0, 2, NULL) < 0)
        return -1;
    put16(p + 0x30, (get16(p + 0x30) & 0xFE00u) | ((u32)sra32(a1, 13) & 0x1FFu));
    if (result)
        *result = at + 0x10u;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001D6930(chan, a1, a2, a3, src): 001D6E60(chan, a1, a2, a3),           */
/* 001D2040(chan, 0), 001D1FF0(chan, 2), then 12 qwords: DMA CNT (qwc    */
/* 0xB), DIRECT 0x5000000A, an A+D block (GIF tag 0x10000000_00008003,   */
/* registers 0xE: (0, 0x3F), (the 0x700 flag when *(ctx+0x9C) == 0 |     */
/* 0xA_24020000, 6), (0x20, 0x3B)), a PACKED GIF tag 0x508B4000_00008001 */
/* with registers 0x43431, the src quadword, and the two corner          */
/* quadwords around the GS centre 0x800 at half the 1 << a2, 1 << a3     */
/* extents. Returns the entry cursor.                                    */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_001D6930(S *s, int32_t chan, int32_t a1, int32_t a2, int32_t a3,
                                    const uint8_t src[16], uint32_t *result)
{
    u32 entry, at, *c, flag;
    int32_t hx, hy;
    uint8_t *p;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001D6930u);
    if (!c)
        return -1;
    /* The whole run (001D6E60 0xB0, two REF tags, 0xC0) is checked first. */
    if (!window(s, *c, 0x190u, 16u, 0x001D6930u))
        return -1;
    if (!src || !s->world.ctx_9C || !s->world.d00241010 || !s->world.d00275674)
        return fault(s, 0x001D6930u, EM_LVP_FAULT_NULL_WORKER);
    entry = *c;
    if (em_load_veil_particles_001D6E60(s, chan, a1, a2, a3, NULL) < 0 ||
        em_load_veil_particles_001D2040(s, chan, 0) < 0 ||
        em_load_veil_particles_001D1FF0(s, chan, 2) < 0)
        return -1;
    hx = sra32((int32_t)(1u << ((u32)a2 & 31u)), 1);
    hy = sra32((int32_t)(1u << ((u32)a3 & 31u)), 1);
    p = open_packet(s, chan, 0xC0u, 0x10, 0xBu, 0x001D6930u, &at);
    flag = *s->world.ctx_9C == 0 ? 0x700u : 0u;
    memset(p + 0x10, 0, 16);
    put32(p + 0x1C, 0x5000000Au);
    put64(p + 0x20, (u64)0x10000000u << 32 | 0x8003u);
    put64(p + 0x28, 0xEu);
    put64(p + 0x30, 0);
    put64(p + 0x38, 0x3Fu);
    put64(p + 0x40, sx((int32_t)flag) | ((u64)0xAu << 32 | 0x24020000u));
    put64(p + 0x48, 6u);
    put64(p + 0x50, 0x20u);
    put64(p + 0x58, 0x3Bu);
    put64(p + 0x60, (u64)0x508B4000u << 32 | 0x8001u);
    put64(p + 0x68, 0x43431u);
    memcpy(p + 0x70, src, 16);
    put32(p + 0x80, 8);
    put32(p + 0x84, 8);
    put32(p + 0x90, (u32)(0x800 - hx) << 4);
    put32(p + 0x94, (u32)(0x800 - hy) << 4);
    put32(p + 0x98, 0);
    put32(p + 0x9C, 0xFFu);
    put32(p + 0xA0, 0x1FF8u);
    put32(p + 0xA4, 0xDF8u);
    put32(p + 0xB0, (u32)(hx + 0x800) << 4);
    put32(p + 0xB4, (u32)(hy + 0x800) << 4);
    put32(p + 0xB8, 0);
    put32(p + 0xBC, 0xFFu);
    if (result)
        *result = entry;
    return 0;
}

/* 001D6B60: 001D6930(...) then 001D1F20(chan); returns 001D6930's value. */
int em_load_veil_particles_001D6B60(S *s, int32_t chan, int32_t a1, int32_t a2, int32_t a3,
                                    const uint8_t src[16], uint32_t *result)
{
    u32 r;
    if (em_load_veil_particles_001D6930(s, chan, a1, a2, a3, src, &r) < 0 ||
        em_load_veil_particles_001D1F20(s, chan) < 0)
        return -1;
    if (result)
        *result = r;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001DFA40(chan, a1, a2, f12): the lens pass.                         */
/* ------------------------------------------------------------------ */

/* f = fabsf(2 * (k / 15) - 1); returns k / 15 in *unit and f * f. */
static int lens_axis(S *s, int32_t k, u32 *unit, u32 *squared)
{
    u32 f;
    *unit = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)k), F_15);
    if (fabs_w(s, em_ee_sub_bits(em_ee_mul_bits(F_TWO, *unit), F_ONE), &f) < 0)
        return -1;
    *squared = em_ee_mul_bits(f, f);
    return 0;
}

static int views_for_lens(S *s)
{
    const EmLoadVeilParticlesWorld *w = &s->world;
    if (!w->ctx_9C || !w->d00275674 || !w->d0027568C || !w->d0026E880 || !w->d00241010)
        return fault(s, 0x001DFA40u, EM_LVP_FAULT_NULL_WORKER);
    if (!w->table)
        return fault(s, 0x001DFA40u, EM_LVP_FAULT_NULL_WORKER);
    if (!s->workers.w_0011DF78)
        return fault(s, 0x0011DF78u, EM_LVP_FAULT_NULL_WORKER);
    if (!s->workers.w_001281C0)
        return fault(s, 0x001281C0u, EM_LVP_FAULT_NULL_WORKER);
    return 0;
}

int em_load_veil_particles_001DFA40(S *s, int32_t chan, uint32_t a1, uint32_t a2, uint32_t f12,
                                    uint32_t *result)
{
    uint8_t *table;
    u32 entry, *c, row, column, v2, u2, k;
    int32_t i, j;
    u64 tag;
    if (latched(s))
        return -1;
    c = cursor_of(s, chan, 0x001DFA40u);
    if (!c || !window(s, *c, EM_LOAD_VEIL_PARTICLES_001DFA40_BYTES, 16u, 0x001DFA40u) ||
        views_for_lens(s) < 0)
        return -1;
    table = s->world.table;
    entry = *c;
    if (em_load_veil_particles_001D6B60(s, chan, (int32_t)*s->world.d0027568C, 8, 8,
                                        s->world.d0026E880, NULL) < 0 ||
        em_load_veil_particles_001D6BA0(s, chan, (int32_t)*s->world.d0027568C, 8, 8, 0, 0, NULL) < 0 ||
        em_load_veil_particles_001D1FF0(s, chan, 3) < 0 ||
        em_load_veil_particles_001D2040(s, chan, 0) < 0 ||
        em_load_veil_particles_001D7080(s, chan, a2, F_ONE) < 0)
        return -1;

    /* The table (001DFB10): entry [i][j] = (0.5 + (j/15 - 0.5) * k,
     * 0.5 + (i/15 - 0.5) * k, 1.0) with k = 0.5 / (1 + (u^2 + v^2) * f12),
     * u = |2 j/15 - 1|, v = |2 i/15 - 1|. Lane 3 is never written. */
    for (i = 0; i < 16; ++i) {
        if (lens_axis(s, i, &row, &v2) < 0)
            return -1;
        for (j = 0; j < 16; ++j) {
            uint8_t *t = table + i * 0x100 + j * 0x10;
            if (lens_axis(s, j, &column, &u2) < 0)
                return -1;
            k = em_ee_div_bits(F_HALF, em_ee_add_bits(F_ONE, em_ee_mul_bits(em_ee_add_bits(u2, v2), f12)));
            put32(t + 0, em_ee_add_bits(F_HALF, em_ee_mul_bits(em_ee_sub_bits(column, F_HALF), k)));
            put32(t + 4, em_ee_add_bits(F_HALF, em_ee_mul_bits(em_ee_sub_bits(row, F_HALF), k)));
            put32(t + 8, F_ONE);
        }
    }

    /* The strips (001DFC20): 15 packets of 0x43 qwords, each a PACKED GIF
     * tag of 16 loops over (ST, XYZF2, ST, XYZF2) = rows i and i + 1. */
    tag = sx((int32_t)(a1 | 0x14u)) << 47 | (u64)0x40004000u << 32 | 0x8010u;
    for (i = 0; i < 15; ++i) {
        u32 at, top[4], bottom[4];
        uint8_t *p = open_packet(s, chan, 0x430u, 0x10, 0x42u, 0x001DFA40u, &at), *v;
        if (!p)
            return -1;
        memset(p + 0x10, 0, 16);
        put32(p + 0x1C, 0x50000041u);
        put64(p + 0x20, tag);
        put64(p + 0x28, 0x4242u);
        top[0] = bottom[0] = 0x7000u;
        top[1] = (u32)((i * 0xE0) / 15 + 0x790) << 4;
        bottom[1] = (u32)(((i + 1) * 0xE0) / 15 + 0x790) << 4;
        top[2] = bottom[2] = 0xFFFFFFu;
        top[3] = bottom[3] = 0;
        v = p + 0x30;
        for (j = 0; j < 16; ++j, v += 0x40) {
            int32_t x;
            int n;
            memcpy(v, table + i * 0x100 + j * 0x10, 16);
            for (n = 0; n < 4; ++n)
                put32(v + 0x10 + 4 * n, top[n]);
            memcpy(v + 0x20, table + (i + 1) * 0x100 + j * 0x10, 16);
            for (n = 0; n < 4; ++n)
                put32(v + 0x30 + 4 * n, bottom[n]);
            if (to_int(s, em_ee_add_bits(em_ee_cvt_s_w_bits(top[0]), F_COLUMN), &x) < 0)
                return -1;
            top[0] = (u32)x;
            if (to_int(s, em_ee_add_bits(em_ee_cvt_s_w_bits(bottom[0]), F_COLUMN), &x) < 0)
                return -1;
            bottom[0] = (u32)x;
        }
    }
    if (em_load_veil_particles_001D1F20(s, chan) < 0 || em_load_veil_particles_001D1FF0(s, chan, 1) < 0)
        return -1;
    if (result)
        *result = entry;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 0021B500: phase += 0.007; if !(phase < 1.0) phase -= 1.0.            */
/* ------------------------------------------------------------------ */
int em_load_veil_particles_0021B500(const EmLoadVeilParticlesBlock *veil)
{
    u32 f;
    if (!veil || !veil->phase)
        return -1;
    f = em_ee_add_bits(*veil->phase, F_PHASE_STEP);
    *veil->phase = f;
    if (!em_ee_c_lt_bits(f, F_ONE))
        *veil->phase = em_ee_sub_bits(*veil->phase, F_ONE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* 0021B1B0: the veil draw.                                            */
/* ------------------------------------------------------------------ */

/* The segment's brightness 0..255 (0021B2A0..0021B34C): the distance
 * behind the phase d = x - phase (wrapped by +1 when negative), d^4 zeroed
 * below 0.1, times +0x08, times 255, float_to_int. */
static int brightness(S *s, const EmLoadVeilParticlesBlock *veil, u32 x, int32_t *out)
{
    u32 d = em_ee_sub_bits(x, *veil->phase), level;
    if (em_ee_c_lt_bits(d, F_ZERO))
        d = em_ee_mul_bits(em_ee_add_bits(d, F_ONE), em_ee_add_bits(d, F_ONE));
    else
        d = em_ee_mul_bits(d, d);
    d = em_ee_mul_bits(d, d);
    if (em_ee_c_lt_bits(d, F_TENTH))
        d = F_ZERO;
    memcpy(&level, veil->level0, sizeof level);
    return to_int(s, em_ee_mul_bits(F_255, em_ee_mul_bits(d, level)), out);
}

int em_load_veil_particles_0021B1B0(S *s, const EmLoadVeilParticlesBlock *veil)
{
    /* The stack quadwords: +0x70 colour and +0x90 position of the segment's
     * first vertex, +0x80 colour and +0xA0 position of its second. */
    u32 q70[4] = {0, 0, 0, 0x80u}, q90[4], q80[4], qA0[4];
    int32_t i;
    if (latched(s))
        return -1;
    if (!veil || !veil->phase || !veil->level0 || !veil->seed || !veil->base_y)
        return fault(s, 0x0021B1B0u, EM_LVP_FAULT_NULL_WORKER);
    {
        u32 *c = cursor_of(s, 0, 0x0021B1B0u);
        if (!c || !window(s, *c, EM_LOAD_VEIL_PARTICLES_0021B1B0_BYTES, 16u, 0x0021B1B0u) ||
            views_for_lens(s) < 0)
            return -1;
    }
    *veil->seed = 0x07234567u;
    q90[0] = 0;
    q90[1] = *veil->base_y;
    q90[2] = 0;
    q90[3] = 0;
    if (em_load_veil_particles_001D1F80(s, 0, 0, 7) < 0)
        return -1;
    for (i = 0; i < 512; ++i) {
        u32 seed = *veil->seed, r = (u32)(i % 5), next = seed * 5u + 1u;
        /* Value noise: the two 16-bit LCG outputs mixed (5 - r) : r, / 5. */
        u32 n = ((5u - r) * (seed >> 16) + (next >> 16) * r) / 5u;
        u32 noise, x, mag, env;
        int32_t y, c;
        int m;
        if ((int32_t)n < 0)   /* the unsigned conversion; n < 2^30, never taken */
            noise = em_ee_add_bits(em_ee_cvt_s_w_bits((n >> 1) | (n & 1u)),
                                   em_ee_cvt_s_w_bits((n >> 1) | (n & 1u)));
        else
            noise = em_ee_cvt_s_w_bits(n);
        noise = em_ee_sub_bits(em_ee_div_bits(noise, F_65536), F_HALF);
        x = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)i), F_512);
        if (brightness(s, veil, x, &c) < 0)
            return -1;
        /* The envelope: ((0.5 - |0.5 - x|) / 0.5)^16, + 0.2 every 64th. */
        if (fabs_w(s, em_ee_sub_bits(F_HALF, x), &mag) < 0)
            return -1;
        env = em_ee_div_bits(em_ee_sub_bits(F_HALF, mag), F_HALF);
        for (m = 0; m < 4; ++m)
            env = em_ee_mul_bits(env, env);
        if ((i & 0x3F) == 0)
            env = em_ee_add_bits(env, F_FIFTH);
        noise = em_ee_mul_bits(em_ee_mul_bits(noise, F_1280), env);
        qA0[0] = (u32)(i + 0x700) << 4;
        if (to_int(s, em_ee_add_bits(em_ee_cvt_s_w_bits(*veil->base_y), noise), &y) < 0)
            return -1;
        qA0[1] = (u32)y;
        qA0[2] = 0xFFFFFFu;
        q80[0] = (u32)sra32((int32_t)((u32)c << 1), 3);
        q80[1] = (u32)sra32((int32_t)(((u32)c << 1) + (u32)c), 3);
        q80[2] = (u32)c;
        qA0[3] = 0;
        q80[3] = (u32)c;
        if (em_load_veil_particles_001D63B0(s, 0, q90, q70, qA0, q80, NULL) < 0)
            return -1;
        if (r == 4)
            *veil->seed = next;
        memcpy(q70, q80, sizeof q70);   /* 00102948(sp+0x70, sp+0x80) */
        memcpy(q90, qA0, sizeof q90);   /* 00102948(sp+0x90, sp+0xA0) */
    }
    if (em_load_veil_particles_001D1F80(s, 0, 0, 7) < 0 ||
        em_load_veil_particles_001DFA40(s, 0, 0, 0x80808080u, F_LENS, NULL) < 0 ||
        em_load_veil_particles_001D1F80(s, 0, 0, 2) < 0 ||
        em_load_veil_particles_001DFA40(s, 0, 0x40u, 0x40404040u, F_LENS, NULL) < 0)
        return -1;
    return 0;
}
