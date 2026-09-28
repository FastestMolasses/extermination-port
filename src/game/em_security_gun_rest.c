/* AREA11 security gun 0x825940 over original record memory: lifecycles 1
 * and 4 with the overlay helpers 0x826F30 / 0x827400, the adapter that runs
 * em_gun_tick for every other lifecycle, and 001B1190 (see
 * em_security_gun_rest.h and docs/SECURITY_GUN.md). Addresses in comments are original
 * runtime addresses (overlay listing address + 0x40); float constants are
 * the original bit patterns. */
#include "game/em_security_gun_rest.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

/* ---- constants ----------------------------------------------------------- */

#define K_ZERO UINT32_C(0x00000000)
#define K_ONE UINT32_C(0x3F800000)
#define K_QUARTER UINT32_C(0x3E800000)
#define K_PI UINT32_C(0x40490FDB)
#define K_NEG_PI UINT32_C(0xC0490FDB)
#define K_YAW_MAX UINT32_C(0x3F91361E)   /* bone 2 +0x74 clamp, +1.1344 */
#define K_YAW_MIN UINT32_C(0xBF91361E)   /* bone 2 +0x74 / bone 3 +0x78 clamp, -1.1344 */
#define K_PITCH_MAX UINT32_C(0xBF060A92) /* bone 3 +0x78 upper clamp (aim) */
#define K_SWAY_BIAS UINT32_C(0xBF543B67) /* bone 3 +0x78 = BIAS + GAIN * sin(+0x1FC) */
#define K_SWAY_GAIN UINT32_C(0x3E9C61AA)
#define K_STEP UINT32_C(0x3C3EA2F1)      /* aim step */
#define K_NEG_STEP UINT32_C(0xBC3EA2F1)
#define K_EPS UINT32_C(0x3A83126F)       /* 0011DF78 result threshold */
#define K_128 UINT32_C(0x43000000)
#define K_RANGE UINT32_C(0x43960000)     /* 300.0: 001FBD50 f12 */
#define K_RANGE_60 UINT32_C(0x42700000)  /* 60.0 */
#define K_SIGHT_LEN UINT32_C(0x42700000) /* 0x826F30: the 60.0 x of the sight vector */
#define K_TIP_X UINT32_C(0x40400000)     /* 0x826F30: gun-tip point (3, -2, 0, 1) */
#define K_TIP_Y UINT32_C(0xC0000000)
#define K_FAR UINT32_C(0x461C4000)       /* 10000.0 */
#define K_FIVE UINT32_C(0x40A00000)
#define K_THREE UINT32_C(0x40400000)
#define K_TWO UINT32_C(0x40000000)
#define K_POINT8 UINT32_C(0x3F4CCCCD)
#define K_HUNDRED UINT32_C(0x42C80000)
#define K_SPRITE_TAG UINT64_C(0x20045BA5154222DC) /* 001CD520 a3 */

#define SP(x) (UINT32_C(0x70000000) + (x))

/* ---- context and fail-stop ------------------------------------------------ */

typedef struct {
    const EmGunRestMem *m;
    const EmGunRestWorkers *w;
    EmGunFault *fault;
    uint32_t a; /* the gun record */
} Hf;

static int hf_fault(EmGunFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_GUN_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

#define TRY(x) do { if ((x) < 0) return -1; } while (0)
#define W(address, name, ...)                                                          \
    do {                                                                               \
        if (!h->w->name) return hf_fault(h->fault, (address), EM_GUN_FAULT_NULL);     \
        if (h->w->name(h->w->ctx, __VA_ARGS__) < 0)                                    \
            return hf_fault(h->fault, (address), EM_GUN_FAULT_WORKER_FAILED);         \
    } while (0)

/* ---- memory ---------------------------------------------------------------- */

static int ldp(Hf *h, uint32_t address, uint32_t size, const uint8_t **p)
{
    *p = h->m->load ? h->m->load(h->m->ctx, address, size) : NULL;
    return *p ? 0 : hf_fault(h->fault, address, EM_GUN_FAULT_NULL);
}

static int stp(Hf *h, uint32_t address, uint32_t size, uint8_t **p)
{
    *p = h->m->store ? h->m->store(h->m->ctx, address, size) : NULL;
    return *p ? 0 : hf_fault(h->fault, address, EM_GUN_FAULT_NULL);
}

static int rd(Hf *h, uint32_t address, uint32_t size, uint32_t *out)
{
    const uint8_t *p;
    TRY(ldp(h, address, size, &p));
    uint32_t v = 0;
    for (uint32_t i = 0; i < size; ++i) v |= (uint32_t)p[i] << (8 * i);
    *out = v;
    return 0;
}

static int wr(Hf *h, uint32_t address, uint32_t size, uint32_t value)
{
    uint8_t *p;
    TRY(stp(h, address, size, &p));
    for (uint32_t i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (8 * i));
    return 0;
}

#define RD32(addr, out) TRY(rd(h, (addr), 4, (out)))
#define RD16(addr, out) TRY(rd(h, (addr), 2, (out)))
#define RD8(addr, out) TRY(rd(h, (addr), 1, (out)))
#define WR32(addr, v) TRY(wr(h, (addr), 4, (v)))
#define WR16(addr, v) TRY(wr(h, (addr), 2, (v)))
#define WR8(addr, v) TRY(wr(h, (addr), 1, (v)))
#define LD(addr, size, p) TRY(ldp(h, (addr), (size), (p)))
#define ST(addr, size, p) TRY(stp(h, (addr), (size), (p)))

static int32_t s16(uint32_t v) { return (int32_t)(int16_t)(uint16_t)v; }

/* 32-bit arithmetic shift right (sra). */
static uint32_t sra32(uint32_t value, unsigned shift)
{
    return (value >> shift) |
           ((value & UINT32_C(0x80000000)) ? ~(UINT32_C(0xFFFFFFFF) >> shift) : 0);
}

/* The bone record slot `i` of D_00275B40 names (the slot is re-read at each
 * use, as the original reloads it). */
static int slot(Hf *h, unsigned i, uint32_t *bone)
{
    uint32_t base;
    RD32(0x00275B40u, &base);
    return rd(h, base + 4u * i, 4, bone);
}

/* *(float *)address = -*(float *)address (neg.s). */
static int negate(Hf *h, uint32_t address)
{
    uint32_t f;
    RD32(address, &f);
    return wr(h, address, 4, em_ee_neg_bits(f));
}

/* ((30 * (rand >> 16)) >> 15): the timer draws of 0x825940. */
static uint32_t rand30(int32_t r)
{
    uint32_t t = sra32((uint32_t)r, 16);
    uint32_t v = (t << 4) - t;
    return sra32(v << 1, 15);
}

/* 300 + ((300 * (rand >> 16)) >> 15) and 60 + ((180 * (rand >> 16)) >> 15). */
static uint32_t rand300(int32_t r)
{
    uint32_t t = sra32((uint32_t)r, 16);
    uint32_t v = (t << 4) - t;
    v = v + (v << 2);
    return sra32(v << 2, 15) + 0x12Cu;
}

static uint32_t rand60(int32_t r)
{
    uint32_t t = sra32((uint32_t)r, 16);
    uint32_t v = (t << 4) - t;
    uint32_t u = (v << 2) - v;
    return sra32(u << 2, 15) + 0x3Cu;
}

static int draw(Hf *h)
{
    uint32_t cb;
    RD32(h->a + 0x4C, &cb);
    W(cb, w_draw_4C, h->a, cb);
    return 0;
}

/* ---- 0x827400: the shot node --------------------------------------------- */

static int shot(Hf *h, uint32_t matrix)
{
    uint32_t node;
    W(0x001AFA90u, w_001AFA90, EM_GUN_LAMP_CLASS, &node);
    if (node == 0) return 0;                                  /* 0x827420 */
    const uint8_t *q, *src, *m;
    uint8_t *dst, local[16];
    LD(EM_GUN_REST_SHOT_QUAD, 16, &q);                        /* 0x827428..0x827430 */
    memcpy(local, q, 16);
    ST(node + 0xB0, 16, &dst);
    LD(matrix + 0x30, 16, &src);
    W(0x00102948u, w_00102948, dst, src);                     /* 0x827440 */
    ST(node + 0xD0, 64, &dst);
    LD(matrix, 64, &src);
    W(0x00102958u, w_00102958, dst, src);                     /* 0x82744C */
    ST(node + 0x100, 16, &dst);
    LD(node + 0xD0, 64, &m);
    W(0x001026A0u, w_001026A0, dst, m, local);                /* 0x82745C */
    WR32(node + 0x10, EM_GUN_REST_SHOT_HANDLER);              /* 0x82746C */
    return 0;
}

/* ---- 0x826F30: the sight probe -------------------------------------------- */

static void put_quad(uint8_t q[16], uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    const uint32_t v[4] = {x, y, z, w};
    for (int i = 0; i < 4; ++i)
        for (int b = 0; b < 4; ++b) q[4 * i + b] = (uint8_t)(v[i] >> (8 * b));
}

static int sight(Hf *h, uint32_t matrix, int32_t *ret)
{
    uint8_t tip[16], local[16], *dst;
    const uint8_t *m, *a, *b;
    uint32_t v, f0;
    int32_t r;

    WR32(SP(0x38A0), K_SIGHT_LEN);                            /* 0x826F50 */
    put_quad(tip, K_TIP_X, K_TIP_Y, K_ZERO, K_ONE);           /* 0x826F58..0x826F70 */
    WR32(SP(0x38A8), 0);
    WR32(SP(0x38A4), 0);
    WR32(SP(0x38AC), 0);
    ST(SP(0x38A0), 16, &dst);
    LD(matrix, 64, &m);
    LD(SP(0x38A0), 16, &a);
    W(0x001026A0u, w_001026A0, dst, m, a);                    /* 0x826FA0 */
    LD(matrix, 64, &m);
    W(0x001026A0u, w_001026A0, tip, m, tip);                  /* 0x826FB0 */
    ST(SP(0x38A0), 16, &dst);
    LD(SP(0x38A0), 16, &a);
    W(0x001028B8u, w_001028B8, dst, a, tip);                  /* 0x826FC8 */
    WR32(SP(0x38AC), K_ONE);
    LD(SP(0x38A0), 16, &a);
    W(0x0019AA80u, w_0019AA80, tip, a, 0x20, &r);             /* 0x826FE8 */
    int s0 = 0;
    if (r != 0) {                                             /* 0x826FF0 */
        ST(SP(0x38A0), 16, &dst);
        LD(SP(0x31B0), 16, &a);
        W(0x00102948u, w_00102948, dst, a);                   /* 0x827004 */
        s0 = 2;
    }
    uint32_t s2, s3, s4;
    RD32(SP(0x31D8), &s2);
    RD32(SP(0x31D4), &s3);
    RD32(SP(0x31D0), &s4);
    LD(SP(0x38A0), 16, &a);
    W(0x0019A570u, w_0019A570, tip, a, 7, 0x20, &r);          /* 0x827040 */
    if (r == 0) {                                             /* 0x827048 -> 0x8270FC */
        WR32(SP(0x31D8), s2);
        WR32(SP(0x31D4), s3);
        WR32(SP(0x31D0), s4);
    } else {
        ST(SP(0x38A0), 16, &dst);
        LD(SP(0x31B0), 16, &a);
        LD(0x00810360u, 16, &b);
        W(0x001028D0u, w_001028D0, dst, a, b);                /* 0x827064 */
        WR32(SP(0x38AC), 0);
        LD(SP(0x38A0), 16, &a);
        W(0x00102738u, w_00102738, a, a, &f0);                /* 0x827080 */
        WR32(SP(0x3A20), f0);
        if (!em_ee_c_le_bits(f0, K_FAR)) {                    /* 0x827098 */
            s0 = 1;
        } else {
            RD32(SP(0x31D8), &v);
            if (v != 1) {                                     /* 0x8270BC */
                s0 = 2;
            } else {
                uint32_t rec;
                RD32(SP(0x31D4), &rec);
                RD8(rec + 3, &v);                             /* 0x8270CC */
                s0 = (v < 0x10 || v >= 0x14) ? 2 : 1;
            }
        }
    }

    if (s0 == 2) {                                            /* 0x827118 */
        uint32_t target, kind;
        RD32(h->a + 0x204, &target);
        if (target == 0) {
            RD32(SP(0x31D8), &kind);
            if (kind == 1) {                                  /* 0x82713C */
                RD32(SP(0x31D4), &v);
                WR32(h->a + 0x204, v);
            }
        }
        uint32_t lc;
        RD8(h->a + 4, &lc);
        if (lc == 4) {                                        /* 0x827158 */
            ST(SP(0x38A0), 16, &dst);
            LD(SP(0x31B0), 16, &a);
            W(0x001031E0u, w_001031E0, dst, a);               /* 0x82716C */
            WR32(SP(0x38AC), K_ONE);
            int32_t rnd;
            W(0x00122BB8u, w_00122BB8, &rnd);                 /* 0x82717C */
            uint32_t t = sra32((uint32_t)rnd, 16);
            uint32_t c = sra32((t << 16) - t, 15);
            c = (sra32(c, 15) & 0x1Fu) + 0x40u;
            WR32(SP(0x38B0), c);                              /* 0x8271A4 */
            WR32(SP(0x38B4), 0);
            WR32(SP(0x38B8), 0);
            WR32(SP(0x38BC), 0x80);
            uint32_t bc, b8, b4, b0;
            RD32(SP(0x38BC), &bc);
            RD32(SP(0x38B8), &b8);
            RD32(SP(0x38B4), &b4);
            RD32(SP(0x38B0), &b0);
            uint32_t colour = b0 | (b4 << 8) | ((bc << 24) | (b8 << 16));
            LD(SP(0x38A0), 16, &a);
            W(0x001CD520u, w_001CD520, 0, 2, a, K_SPRITE_TAG, colour, K_THREE, K_THREE,
              K_TWO);                                         /* 0x827234 */
            WR32(SP(0x38C0), K_POINT8);
            WR32(SP(0x38C8), 0);
            WR32(SP(0x38C4), 0);
            LD(SP(0x38A0), 16, &a);
            LD(SP(0x38C0), 16, &b);
            W(0x001E2BA0u, w_001E2BA0, tip, a, b, K_HUNDRED); /* 0x827274 */
        } else {
            RD32(h->a + 0x200, &v);
            if ((int32_t)v >= 0xD) {                          /* 0x82728C */
                LD(EM_GUN_REST_SIGHT_QUAD, 16, &a);           /* 0x827294..0x8272B0 */
                memcpy(local, a, 16);
                ST(SP(0x38A0), 16, &dst);
                LD(SP(0x31B0), 16, &a);
                W(0x001031E0u, w_001031E0, dst, a);           /* 0x8272B4 */
                WR32(SP(0x38AC), K_ONE);
                uint32_t bone;
                TRY(slot(h, 3, &bone));
                ST(SP(0x38C0), 16, &dst);
                LD(bone + 0x90, 64, &m);
                W(0x001026A0u, w_001026A0, dst, m, local);    /* 0x8272DC */
                WR32(SP(0x38CC), K_ONE);
                WR32(SP(0x38D8), K_POINT8);
                WR32(SP(0x38D4), K_POINT8);
                WR32(SP(0x38D0), K_POINT8);
                const uint8_t *c2;
                LD(SP(0x38A0), 16, &a);
                LD(SP(0x38C0), 16, &b);
                LD(SP(0x38D0), 16, &c2);
                W(0x001E2BA0u, w_001E2BA0, a, b, c2, K_HUNDRED); /* 0x82732C */
            }
        }
        uint32_t tgt, hit;
        RD32(h->a + 0x204, &tgt);                             /* 0x8273B8 */
        RD32(SP(0x31D4), &hit);
        *ret = tgt == hit ? 2 : 1;
        return 0;
    }
    if (s0 == 1) {                                            /* 0x827348 */
        ST(SP(0x38B0), 16, &dst);
        LD(SP(0x31B0), 16, &a);
        W(0x001031E0u, w_001031E0, dst, a);                   /* 0x827354 */
        WR32(SP(0x38BC), K_ONE);
        WR32(SP(0x38C0), K_POINT8);
        WR32(SP(0x38C8), 0);
        WR32(SP(0x38C4), 0);
        LD(SP(0x38B0), 16, &a);
        LD(SP(0x38C0), 16, &b);
        W(0x001E2BA0u, w_001E2BA0, tip, a, b, K_HUNDRED);     /* 0x8273A0 */
    }
    *ret = 0;
    return 0;
}

/* ---- shared pieces of lifecycles 1 and 4 ---------------------------------- */

/* The owner tail with the hull: 001C6380, 001A2370(bone 3 + 0x90), 001B17A0,
 * the +0x4C draw. */
static int tail_hull(Hf *h)
{
    uint32_t bone;
    const uint8_t *m;
    W(0x001C6380u, w_001C6380, h->a);
    TRY(slot(h, 3, &bone));
    LD(bone + 0x90, 64, &m);
    W(0x001A2370u, w_001A2370, h->a, m);
    W(0x001B17A0u, w_001B17A0, h->a);
    return draw(h);
}

/* The child's +0xA0 quad: (x, y, 0, w) through the node at +0x220. */
static int child_a0(Hf *h, uint32_t x, uint32_t y, uint32_t w)
{
    uint32_t c;
    RD32(h->a + 0x220, &c);
    WR32(c + 0xA0, x);
    RD32(h->a + 0x220, &c);
    WR32(c + 0xA4, y);
    RD32(h->a + 0x220, &c);
    WR32(c + 0xA8, 0);
    RD32(h->a + 0x220, &c);
    WR32(c + 0xAC, w);
    return 0;
}

/* 00102958(child +0x11C bone + 0x90, gun +0x11C bone + 0x90). */
static int copy_bone_to_child(Hf *h)
{
    uint32_t c, self_bone, child_bone;
    const uint8_t *src;
    uint8_t *dst;
    RD32(h->a + 0x220, &c);
    RD32(h->a + 0x11C, &self_bone);
    RD32(c + 0x11C, &child_bone);
    ST(child_bone + 0x90, 64, &dst);
    LD(self_bone + 0x90, 64, &src);
    W(0x00102958u, w_00102958, dst, src);
    return 0;
}

/* The shot reaction: +0x208 = 480 and two fresh sway timers (0x8260B0 /
 * 0x826C40), when +0x36 is set and +0x208 is 0. */
static int shot_reset(Hf *h)
{
    uint32_t hit, alert, v;
    int32_t r;
    RD16(h->a + 0x36, &hit);
    if (hit == 0) return 0;
    RD32(h->a + 0x208, &alert);
    if (alert != 0) return 0;
    WR32(h->a + 0x208, 0x1E0);
    W(0x00122BB8u, w_00122BB8, &r);
    WR32(h->a + 0x20C, rand30(r));
    RD32(h->a + 0x20C, &v);
    if (v & 8) TRY(negate(h, h->a + 0x210));
    W(0x00122BB8u, w_00122BB8, &r);
    WR32(h->a + 0x214, rand30(r));
    RD32(h->a + 0x214, &v);
    if (v & 4) TRY(negate(h, h->a + 0x218));
    return 0;
}

/* The alert block (+0x208 > 0) of both lifecycles: 0x825B7C..0x825E20 (4)
 * and 0x826198..0x826434 (1). `lifecycle` picks the countdown's axis. */
static int alert(Hf *h, int lifecycle)
{
    uint32_t v, bone, f;
    int32_t r;
    RD32(h->a + 0x208, &v);
    WR32(h->a + 0x208, v - 1u);
    RD32(h->a + 0x208, &v);
    if ((int32_t)v < 0x1F) {
        /* 0x825D80 / 0x82639C: the last 31 ticks ramp the child's vector. */
        uint32_t n = UINT32_C(0x80) - (v << 2);
        uint32_t q = em_ee_div_bits(em_ee_cvt_s_w_bits(n), K_128);
        WR32(SP(0x3A20), q);
        if (lifecycle == 1) {
            TRY(child_a0(h, q, 0, K_QUARTER));
        } else {
            uint32_t c, x;
            RD32(h->a + 0x220, &c);
            WR32(c + 0xA0, 0);
            RD32(SP(0x3A20), &x);
            RD32(h->a + 0x220, &c);
            WR32(c + 0xA4, x);
            RD32(h->a + 0x220, &c);
            WR32(c + 0xA8, 0);
            RD32(h->a + 0x220, &c);
            WR32(c + 0xAC, K_QUARTER);
        }
        TRY(copy_bone_to_child(h));
    } else {
        /* The head sway: bone 2 +0x74 += +0x210 between the clamps. */
        RD32(h->a + 0x20C, &v);
        WR32(h->a + 0x20C, v - 1u);
        if ((int32_t)(v - 1u) > 0) {
            uint32_t rate;
            RD32(h->a + 0x210, &rate);
            TRY(slot(h, 2, &bone));
            RD32(bone + 0x74, &f);
            WR32(bone + 0x74, em_ee_add_bits(f, rate));
            TRY(slot(h, 2, &bone));
            RD32(bone + 0x74, &f);
            if (!em_ee_c_le_bits(f, K_YAW_MAX)) {
                WR32(bone + 0x74, K_YAW_MAX);
                TRY(negate(h, h->a + 0x210));
            }
            TRY(slot(h, 2, &bone));
            RD32(bone + 0x74, &f);
            if (em_ee_c_lt_bits(f, K_YAW_MIN)) {
                WR32(bone + 0x74, K_YAW_MIN);
                TRY(negate(h, h->a + 0x210));
            }
        } else {
            W(0x001FBD50u, w_001FBD50, h->a, 0x428, 0, K_RANGE);
            W(0x00122BB8u, w_00122BB8, &r);
            WR32(h->a + 0x20C, rand30(r));
            RD32(h->a + 0x20C, &v);
            if (v & 4) TRY(negate(h, h->a + 0x210));
        }
        /* The gun sway: +0x1FC += +0x218 (wrapped at pi), bone 3 +0x78. */
        RD32(h->a + 0x214, &v);
        WR32(h->a + 0x214, v - 1u);
        if ((int32_t)(v - 1u) > 0) {
            uint32_t rate, angle, s;
            RD32(h->a + 0x218, &rate);
            RD32(h->a + 0x1FC, &angle);
            angle = em_ee_add_bits(angle, rate);
            WR32(h->a + 0x1FC, angle);
            if (!em_ee_c_le_bits(angle, K_PI)) WR32(h->a + 0x1FC, K_NEG_PI);
            RD32(h->a + 0x1FC, &angle);
            W(0x0011E2A8u, w_0011E2A8, angle, &s);
            TRY(slot(h, 3, &bone));
            WR32(bone + 0x78, em_ee_add_bits(K_SWAY_BIAS, em_ee_mul_bits(K_SWAY_GAIN, s)));
        } else {
            W(0x00122BB8u, w_00122BB8, &r);
            WR32(h->a + 0x214, rand30(r));
            RD32(h->a + 0x214, &v);
            if (v & 8) TRY(negate(h, h->a + 0x218));
        }
        TRY(child_a0(h, 0, 0, 0));
    }
    TRY(tail_hull(h));
    WR16(h->a + 0x36, 0);
    return 1;
}

/* ---- lifecycle 4 (0x825B74): patrol --------------------------------------- */

static int lifecycle4(Hf *h)
{
    uint32_t v, f, bone;
    int32_t r;
    RD32(h->a + 0x208, &v);
    if ((int32_t)v > 0) return alert(h, 4);

    TRY(child_a0(h, 0, K_ONE, K_QUARTER));                    /* 0x825E24 */
    TRY(copy_bone_to_child(h));
    RD16(h->a + 0x28, &v);
    WR16(h->a + 0x28, v - 1u);                                /* 0x825E64 */
    RD16(h->a + 0x28, &v);
    if (s16(v) < 0) {
        uint32_t toggle;
        RD32(h->a + 0x200, &toggle);
        W(0x00122BB8u, w_00122BB8, &r);
        WR16(h->a + 0x28, toggle != 0 ? rand60(r) : rand300(r));
        RD32(h->a + 0x200, &toggle);
        WR32(h->a + 0x200, toggle == 0 ? 1u : 0u);            /* 0x825EE8 */
    }
    RD32(h->a + 0x200, &v);
    if (v != 0) {                                             /* 0x825EF8 */
        uint32_t t, rate, angle, s;
        RD16(h->a + 0x28, &t);
        if (((uint32_t)s16(t) & 0x2Fu) == 2)
            W(0x001FBD50u, w_001FBD50, h->a, 0x423, 0, K_RANGE_60);
        RD32(h->a + 0x1F4, &rate);
        TRY(slot(h, 2, &bone));
        RD32(bone + 0x74, &f);
        WR32(bone + 0x74, em_ee_add_bits(f, rate));
        RD32(h->a + 0x1F4, &rate);
        if (!em_ee_c_le_bits(rate, K_ZERO)) {
            TRY(slot(h, 2, &bone));
            RD32(bone + 0x74, &f);
            if (!em_ee_c_le_bits(f, K_YAW_MAX)) {
                WR32(bone + 0x74, K_YAW_MAX);
                TRY(negate(h, h->a + 0x1F4));
            }
        } else {
            TRY(slot(h, 2, &bone));
            RD32(bone + 0x74, &f);
            if (em_ee_c_lt_bits(f, K_YAW_MIN)) {
                WR32(bone + 0x74, K_YAW_MIN);
                TRY(negate(h, h->a + 0x1F4));
            }
        }
        RD32(h->a + 0x1F8, &rate);
        RD32(h->a + 0x1FC, &angle);
        angle = em_ee_add_bits(angle, rate);
        WR32(h->a + 0x1FC, angle);
        if (!em_ee_c_le_bits(angle, K_PI)) WR32(h->a + 0x1FC, K_NEG_PI);
        RD32(h->a + 0x1FC, &angle);
        W(0x0011E2A8u, w_0011E2A8, angle, &s);
        TRY(slot(h, 3, &bone));
        WR32(bone + 0x78, em_ee_add_bits(K_SWAY_BIAS, em_ee_mul_bits(K_SWAY_GAIN, s)));
    }
    TRY(tail_hull(h));                                        /* 0x826054 */
    int32_t seen;
    TRY(slot(h, 3, &bone));
    TRY(sight(h, bone + 0x90, &seen));                        /* 0x826090 */
    TRY(shot_reset(h));
    WR16(h->a + 0x36, 0);                                     /* 0x82612C */
    RD32(h->a + 0x204, &v);
    if (v == 0) return 1;
    WR8(h->a + 4, 1);                                         /* 0x826140: to aim */
    WR32(h->a + 0x224, 1);
    WR16(h->a + 0x28, (uint32_t)-0x2B);
    WR16(h->a + 0x2A, 0x12C);
    WR32(h->a + 0x200, 0);
    RD32(SP(0x3B68), &v);
    if ((v & 0x3F) == 0) W(0x001FBD50u, w_001FBD50, h->a, 0x424, 0, K_RANGE);
    return 1;
}

/* ---- lifecycle 1 (0x826190): aim and fire ---------------------------------- */

static int fx(Hf *h, uint32_t id)
{
    const uint8_t *p, *at;
    uint32_t node;
    LD(SP(0x38A0), 16, &p);
    LD(SP(0x38B0), 16, &at);
    W(0x001EFD90u, w_001EFD90, id, p, at, &node);
    return 0;
}

/* 0x700038A0 = the probe point, 0x700038B0 = the hit face's +0x24..+0x2C
 * with w 1.0, 0x700038AC = 1.0. */
static int hit_point(Hf *h, uint32_t face)
{
    uint32_t v;
    RD32(face + 0x24, &v);
    WR32(SP(0x38B0), v);
    RD32(face + 0x28, &v);
    WR32(SP(0x38B4), v);
    RD32(face + 0x2C, &v);
    WR32(SP(0x38B8), v);
    WR32(SP(0x38BC), K_ONE);
    WR32(SP(0x38AC), K_ONE);
    return 0;
}

/* 0x8268D8..0x826C04: the shot and what it hit. */
static int fire(Hf *h)
{
    uint32_t bone, face, kind, v;
    uint8_t *dst;
    const uint8_t *a, *b;
    W(0x001FBD50u, w_001FBD50, h->a, 0x425, 0, K_RANGE);
    TRY(slot(h, 3, &bone));
    TRY(shot(h, bone + 0x90));
    ST(SP(0x38A0), 16, &dst);
    LD(SP(0x31B0), 16, &a);
    W(0x00102948u, w_00102948, dst, a);                       /* 0x826934 */
    RD32(SP(0x31D0), &face);
    RD32(SP(0x31D8), &kind);
    TRY(hit_point(h, face));
    if (kind == 1) {                                          /* 0x826984: an actor */
        uint32_t hit;
        RD32(SP(0x31D4), &hit);
        RD8(hit + 2, &v);
        if ((v & 0x1F) != 0) {                                /* 0x82699C */
            TRY(fx(h, UINT32_C(0x80000007)));
            RD32(SP(0x31D4), &hit);
            WR16(hit + 0x36, 5);
        } else {
            RD8(hit + 0, &v);
            if ((v & 2) == 0) {                               /* 0x8269AC: the player */
                TRY(fx(h, UINT32_C(0x80000006)));
                RD32(SP(0x31D4), &hit);
                WR32(hit + 0x224, K_FIVE);
                RD32(SP(0x31D4), &hit);
                RD8(hit + 0, &v);
                WR8(hit + 0, v | 2);
                ST(SP(0x3910), 16, &dst);
                LD(SP(0x31A0), 16, &a);
                LD(SP(0x3190), 16, &b);
                W(0x001028D0u, w_001028D0, dst, a, b);        /* 0x826A08 */
                WR32(SP(0x391C), 0);
                ST(SP(0x3910), 16, &dst);
                LD(SP(0x3910), 16, &a);
                W(0x00102760u, w_00102760, dst, a);           /* 0x826A24 */
                RD32(SP(0x31D4), &hit);
                ST(hit + 0x70, 16, &dst);
                LD(SP(0x3910), 16, &a);
                W(0x00102948u, w_00102948, dst, a);           /* 0x826A3C */
            }
        }
    } else {                                                  /* 0x826A7C: the world */
        uint32_t surface;
        int32_t r;
        RD8(face + 0x1A, &surface);
        LD(SP(0x3190), 16, &a);
        LD(SP(0x31A0), 16, &b);
        W(0x0019B6C0u, w_0019B6C0, a, b, &r);                 /* 0x826A94 */
        if (r != 0) {
            ST(SP(0x38A0), 16, &dst);
            LD(SP(0x31B0), 16, &a);
            W(0x00102948u, w_00102948, dst, a);               /* 0x826AB0 */
            RD32(SP(0x31D0), &face);
            TRY(hit_point(h, face));
            RD8(face + 0x1A, &v);
            uint32_t id = v == 0x5C ? UINT32_C(0x80000067)
                        : v == 0x5B ? UINT32_C(0x80000026)
                        : v == 0x5A ? UINT32_C(0x8000002C) : UINT32_C(0x80000003);
            TRY(fx(h, id));
        } else {
            TRY(fx(h, surface == 5 ? UINT32_C(0x8000002C) : UINT32_C(0x80000003)));
        }
    }
    return 0;
}

/* The aim at the target +0x204 (0x826438..0x826870). */
static int aim(Hf *h)
{
    uint32_t target, bone, f, x, z, d;
    uint8_t *dst;
    const uint8_t *a, *b;

    RD32(SP(0x3B68), &x);
    if ((x & 0x3F) == 0) W(0x001FBD50u, w_001FBD50, h->a, 0x424, 0, K_RANGE);
    TRY(child_a0(h, K_ONE, 0, K_QUARTER));                    /* 0x826460 */
    TRY(copy_bone_to_child(h));
    /* The heading: bone 2 +0x74 turns toward the target by K_STEP, or snaps
     * to the heading the SDK atan gives when the target is ahead. */
    RD32(h->a + 0x204, &target);
    TRY(slot(h, 2, &bone));
    ST(SP(0x3600), 16, &dst);
    LD(target + 0xB0, 16, &a);
    LD(bone + 0xC0, 16, &b);
    W(0x001028D0u, w_001028D0, dst, a, b);                    /* 0x8264C0 */
    ST(SP(0x3610), 16, &dst);
    LD(SP(0x3600), 16, &a);
    W(0x00102948u, w_00102948, dst, a);                       /* 0x8264D4 */
    WR32(SP(0x360C), 0);
    WR32(SP(0x3604), 0);
    ST(SP(0x3600), 16, &dst);
    LD(SP(0x3600), 16, &a);
    W(0x00102760u, w_00102760, dst, a);                       /* 0x8264F8 */
    uint32_t f3, f4, f2, f1;
    RD32(SP(0x3600), &f3);
    TRY(slot(h, 2, &bone));
    RD32(bone + 0xB0, &f4);
    RD32(bone + 0xB8, &f2);
    RD32(SP(0x3608), &f1);
    d = em_ee_madd_bits(em_ee_mula_bits(f3, em_ee_neg_bits(f4)), f1, em_ee_neg_bits(f2));
    WR32(SP(0x3680), d);                                      /* 0x826550 */
    if (em_ee_c_lt_bits(d, K_NEG_STEP)) {
        TRY(slot(h, 2, &bone));
        RD32(bone + 0x74, &f);
        WR32(bone + 0x74, em_ee_sub_bits(f, K_STEP));         /* 0x826570 */
    } else if (!em_ee_c_le_bits(d, K_STEP)) {
        TRY(slot(h, 2, &bone));
        RD32(bone + 0x74, &f);
        WR32(bone + 0x74, em_ee_add_bits(f, K_STEP));         /* 0x8265A4 */
    } else {
        uint32_t ax;
        RD32(SP(0x3610), &x);
        W(0x0011DF78u, w_0011DF78, x, &ax);                   /* 0x8265AC */
        if (!em_ee_c_le_bits(ax, K_EPS)) {
            uint32_t q, at, heading, wrapped, rot;
            RD32(SP(0x3610), &x);
            RD32(SP(0x3618), &z);
            q = em_ee_div_bits(z, x);
            WR32(SP(0x3684), q);
            W(0x0011DBB8u, w_0011DBB8, q, &at);
            heading = em_ee_c_lt_bits(x, K_ZERO) ? em_ee_sub_bits(K_PI, at) : em_ee_neg_bits(at);
            WR32(SP(0x3684), heading);
            RD32(h->a + 0xC4, &rot);
            RD32(SP(0x3684), &heading);
            W(0x001B1470u, w_001B1470, em_ee_sub_bits(heading, rot), &wrapped);
            TRY(slot(h, 2, &bone));
            WR32(bone + 0x74, wrapped);                       /* 0x826648 / 0x826690 */
        }
    }
    uint32_t t;
    TRY(slot(h, 2, &bone));
    RD32(bone + 0x74, &f);
    if (em_ee_c_lt_bits(f, K_YAW_MIN)) {                      /* 0x826694 */
        RD16(h->a + 0x2A, &t);
        WR16(h->a + 0x2A, t - 4u);
        TRY(slot(h, 2, &bone));
        WR32(bone + 0x74, K_YAW_MIN);
    }
    TRY(slot(h, 2, &bone));
    RD32(bone + 0x74, &f);
    if (!em_ee_c_le_bits(f, K_YAW_MAX)) {                     /* 0x8266D4 */
        RD16(h->a + 0x2A, &t);
        WR16(h->a + 0x2A, t - 4u);
        TRY(slot(h, 2, &bone));
        WR32(bone + 0x74, K_YAW_MAX);
    }
    /* The pitch: atan(dy / |dxz|), bone 3 +0x78 steps toward it. */
    ST(SP(0x3600), 16, &dst);
    LD(SP(0x3610), 16, &a);
    W(0x00102948u, w_00102948, dst, a);                       /* 0x826720 */
    uint32_t len, pitch, y, cur;
    RD32(SP(0x3600), &x);
    RD32(SP(0x3608), &z);
    W(0x0011E748u, w_0011E748, em_ee_madd_bits(em_ee_mula_bits(x, x), z, z), &len);
    WR32(SP(0x3680), len);
    RD32(SP(0x3604), &y);
    RD32(SP(0x3680), &len);
    W(0x0011DBB8u, w_0011DBB8, em_ee_div_bits(y, len), &pitch); /* 0x826768 */
    WR32(SP(0x3680), pitch);
    RD32(SP(0x3680), &pitch);
    TRY(slot(h, 3, &bone));
    RD32(bone + 0x78, &cur);
    if (!em_ee_c_le_bits(pitch, em_ee_sub_bits(cur, K_STEP))) {
        RD32(bone + 0x78, &f);
        WR32(bone + 0x78, em_ee_add_bits(f, K_STEP));         /* 0x8267B4 */
    } else if (em_ee_c_lt_bits(pitch, em_ee_add_bits(K_STEP, cur))) {
        RD32(bone + 0x78, &f);
        WR32(bone + 0x78, em_ee_sub_bits(f, K_STEP));         /* 0x8267D8 */
    } else {
        WR32(bone + 0x78, pitch);                             /* 0x8267E4 */
    }
    TRY(slot(h, 3, &bone));
    RD32(bone + 0x78, &f);
    if (em_ee_c_lt_bits(f, K_YAW_MIN)) {                      /* 0x826808 */
        RD16(h->a + 0x2A, &t);
        WR16(h->a + 0x2A, t - 4u);
        TRY(slot(h, 3, &bone));
        WR32(bone + 0x78, K_YAW_MIN);
    }
    TRY(slot(h, 3, &bone));
    RD32(bone + 0x78, &f);
    if (!em_ee_c_le_bits(f, K_PITCH_MAX)) {                   /* 0x826848 */
        RD16(h->a + 0x2A, &t);
        WR16(h->a + 0x2A, t - 4u);
        TRY(slot(h, 3, &bone));
        WR32(bone + 0x78, K_PITCH_MAX);
    }
    return 0;
}

static int lifecycle1(Hf *h)
{
    uint32_t v, t, bone;
    int32_t r;
    RD32(h->a + 0x208, &v);
    if ((int32_t)v > 0) return alert(h, 1);

    TRY(aim(h));
    W(0x001C6380u, w_001C6380, h->a);                         /* 0x82686C (no hull) */
    W(0x001B17A0u, w_001B17A0, h->a);
    TRY(draw(h));
    RD16(h->a + 0x28, &t);
    WR16(h->a + 0x28, t + 1u);                                /* 0x826894 */
    int32_t seen;
    TRY(slot(h, 3, &bone));
    TRY(sight(h, bone + 0x90, &seen));                        /* 0x8268A0 */
    if (seen == 2) {
        WR16(h->a + 0x2A, 0x12C);
    } else {
        RD16(h->a + 0x2A, &t);
        WR16(h->a + 0x2A, t - 1u);
    }
    RD32(h->a + 0x200, &v);
    if (v != 0) {                                             /* 0x8268D4 */
        WR32(h->a + 0x200, v + 1u);
        RD32(h->a + 0x200, &v);
        if ((int32_t)v >= 0xE) {
            if (seen != 0) TRY(fire(h));
            WR32(h->a + 0x200, 1);                            /* 0x826C0C */
        }
    } else {
        RD16(h->a + 0x28, &t);
        if (s16(t) > 0) {                                     /* 0x826C14 */
            WR32(h->a + 0x200, 1);
            WR16(h->a + 0x28, 0);
        }
    }
    TRY(shot_reset(h));                                       /* 0x826C28 */
    WR16(h->a + 0x36, 0);
    RD16(h->a + 0x2A, &t);
    if (s16(t) >= 0) return 1;
    /* 0x826CCC: lost the target; back to patrol with the gun sway angle
     * recovered from bone 3 +0x78. */
    uint32_t pitch, back, wrapped;
    WR8(h->a + 4, 4);
    WR32(h->a + 0x224, 0);
    WR32(h->a + 0x204, 0);
    TRY(slot(h, 3, &bone));
    RD32(bone + 0x78, &pitch);
    W(0x0011E520u, w_0011E520,
      em_ee_div_bits(em_ee_neg_bits(em_ee_sub_bits(pitch, K_YAW_MIN)), K_SWAY_BIAS), &back);
    W(0x001B1470u, w_001B1470, back, &wrapped);
    WR32(h->a + 0x1FC, wrapped);
    W(0x00122BB8u, w_00122BB8, &r);
    WR16(h->a + 0x28, rand300(r));
    WR32(h->a + 0x200, 1);
    return 1;
}

/* ---- the other lifecycles: em_gun_tick over this memory ---------- */

typedef struct {
    Hf *h;
    EmGun v;
    EmGunLamp c;
    uint32_t child;   /* the open child view's node, or 0 */
    float spad3A20;
    uint8_t flags[256];
} Adapt;

static uint32_t fbits(float f) { return em_ee_bits(f); }
static float bitsf(uint32_t b) { return em_ee_float(b); }

/* Load the views from memory. */
static int view_load(Adapt *d)
{
    Hf *h = d->h;
    uint32_t v;
    RD8(h->a + 0x00, &v); d->v.b00 = (uint8_t)v;
    RD8(h->a + 0x04, &v); d->v.lifecycle = (uint8_t)v;
    RD16(h->a + 0x28, &v); d->v.timer_28 = (int16_t)s16(v);
    for (int i = 0; i < 4; ++i) {
        RD32(h->a + 0xB0 + 4u * (unsigned)i, &d->v.pos_B0[i]);
        RD32(h->a + 0xC0 + 4u * (unsigned)i, &d->v.rot_C0[i]);
    }
    RD32(h->a + 0x11C, &d->v.bone3_11C);
    RD32(h->a + 0x1F4, &v); d->v.f1F4 = bitsf(v);
    RD32(h->a + 0x1F8, &v); d->v.f1F8 = bitsf(v);
    RD32(h->a + 0x1FC, &v); d->v.f1FC = bitsf(v);
    RD32(h->a + 0x200, &d->v.w200);
    RD32(h->a + 0x204, &d->v.w204);
    RD32(h->a + 0x208, &d->v.w208);
    RD32(h->a + 0x20C, &d->v.w20C);
    RD32(h->a + 0x210, &v); d->v.f210 = bitsf(v);
    RD32(h->a + 0x214, &v); d->v.f214 = bitsf(v);
    RD32(h->a + 0x218, &v); d->v.f218 = bitsf(v);
    RD32(h->a + 0x21C, &v); d->v.w21C = (int32_t)v;
    RD32(h->a + 0x220, &d->v.child_220);
    RD32(h->a + 0x224, &v); d->v.w224 = (int32_t)v;
    RD32(SP(0x3A20), &v); d->spad3A20 = bitsf(v);
    if (d->child) {
        uint32_t c = d->child;
        RD8(c + 0x03, &v); d->c.b03 = (uint8_t)v;
        RD8(c + 0x0D, &v); d->c.b0D = (uint8_t)v;
        RD16(c + 0x0E, &v); d->c.h0E = (uint16_t)v;
        RD32(c + 0x10, &d->c.handler_10);
        RD16(c + 0x2E, &v); d->c.h2E = (uint16_t)v;
        RD16(c + 0x54, &v); d->c.h54 = (uint16_t)v;
        RD16(c + 0x56, &v); d->c.h56 = (uint16_t)v;
        RD8(c + 0x9A, &v); d->c.b9A = (uint8_t)v;
        for (int i = 0; i < 4; ++i) {
            RD32(c + 0xA0 + 4u * (unsigned)i, &v); d->c.fA0[i] = bitsf(v);
            RD32(c + 0xB0 + 4u * (unsigned)i, &d->c.pos_B0[i]);
            RD32(c + 0xC0 + 4u * (unsigned)i, &d->c.rot_C0[i]);
        }
        RD32(c + 0x11C, &d->c.bone3_11C);
    }
    return 0;
}

/* Store `value` when it differs from memory (only what the module changed
 * reaches memory). */
static int sync(Hf *h, uint32_t address, uint32_t size, uint32_t value)
{
    uint32_t cur;
    TRY(rd(h, address, size, &cur));
    uint32_t mask = size == 4 ? UINT32_C(0xFFFFFFFF) : (UINT32_C(1) << (8 * size)) - 1u;
    return (cur & mask) == (value & mask) ? 0 : wr(h, address, size, value);
}

static int view_flush(Adapt *d)
{
    Hf *h = d->h;
    TRY(sync(h, h->a + 0x00, 1, d->v.b00));
    TRY(sync(h, h->a + 0x04, 1, d->v.lifecycle));
    TRY(sync(h, h->a + 0x28, 2, (uint16_t)d->v.timer_28));
    TRY(sync(h, h->a + 0x1F4, 4, fbits(d->v.f1F4)));
    TRY(sync(h, h->a + 0x1F8, 4, fbits(d->v.f1F8)));
    TRY(sync(h, h->a + 0x1FC, 4, fbits(d->v.f1FC)));
    TRY(sync(h, h->a + 0x200, 4, d->v.w200));
    TRY(sync(h, h->a + 0x204, 4, d->v.w204));
    TRY(sync(h, h->a + 0x208, 4, d->v.w208));
    TRY(sync(h, h->a + 0x20C, 4, d->v.w20C));
    TRY(sync(h, h->a + 0x210, 4, fbits(d->v.f210)));
    TRY(sync(h, h->a + 0x214, 4, fbits(d->v.f214)));
    TRY(sync(h, h->a + 0x218, 4, fbits(d->v.f218)));
    TRY(sync(h, h->a + 0x21C, 4, (uint32_t)d->v.w21C));
    TRY(sync(h, h->a + 0x220, 4, d->v.child_220));
    TRY(sync(h, h->a + 0x224, 4, (uint32_t)d->v.w224));
    TRY(sync(h, SP(0x3A20), 4, fbits(d->spad3A20)));
    if (d->child) {
        uint32_t c = d->child;
        TRY(sync(h, c + 0x03, 1, d->c.b03));
        TRY(sync(h, c + 0x0D, 1, d->c.b0D));
        TRY(sync(h, c + 0x0E, 2, d->c.h0E));
        TRY(sync(h, c + 0x10, 4, d->c.handler_10));
        TRY(sync(h, c + 0x2E, 2, d->c.h2E));
        TRY(sync(h, c + 0x54, 2, d->c.h54));
        TRY(sync(h, c + 0x56, 2, d->c.h56));
        TRY(sync(h, c + 0x9A, 1, d->c.b9A));
        for (int i = 0; i < 4; ++i) {
            TRY(sync(h, c + 0xA0 + 4u * (unsigned)i, 4, fbits(d->c.fA0[i])));
            TRY(sync(h, c + 0xB0 + 4u * (unsigned)i, 4, d->c.pos_B0[i]));
            TRY(sync(h, c + 0xC0 + 4u * (unsigned)i, 4, d->c.rot_C0[i]));
        }
    }
    return 0;
}

/* Every worker call: flush the views, call, reload them. */
#define AD(ctx) Adapt *d = (Adapt *)(ctx); Hf *h = d->h
#define BEFORE() TRY(view_flush(d))
#define AFTER() TRY(view_load(d))
#define CALLA(address, name, ...)                                                      \
    do {                                                                               \
        BEFORE();                                                                      \
        if (!h->w->name) return hf_fault(h->fault, (address), EM_GUN_FAULT_NULL);     \
        if (h->w->name(h->w->ctx, __VA_ARGS__) < 0) return -1;                         \
        AFTER();                                                                       \
    } while (0)

static int a_001C6380(void *ctx) { AD(ctx); CALLA(0x001C6380u, w_001C6380, h->a); return 0; }
static int a_001B17A0(void *ctx) { AD(ctx); CALLA(0x001B17A0u, w_001B17A0, h->a); return 0; }
static int a_001AFC10(void *ctx) { AD(ctx); CALLA(0x001AFC10u, w_001AFC10, h->a); return 0; }

static int a_draw(void *ctx)
{
    AD(ctx);
    uint32_t cb;
    RD32(h->a + 0x4C, &cb);
    CALLA(cb, w_draw_4C, h->a, cb);
    return 0;
}

static int a_001B0FD0(void *ctx, int32_t *result)
{
    AD(ctx);
    CALLA(0x001B0FD0u, w_001B0FD0, h->a, result);
    return 0;
}

static int a_00122BB8(void *ctx, int32_t *value)
{
    AD(ctx);
    CALLA(0x00122BB8u, w_00122BB8, value);
    return 0;
}

static int a_0011E2A8(void *ctx, float x, float *result)
{
    AD(ctx);
    uint32_t r;
    CALLA(0x0011E2A8u, w_0011E2A8, fbits(x), &r);
    *result = bitsf(r);
    return 0;
}

static int a_00275B40(void *ctx, uint32_t index, uint32_t *bone)
{
    AD(ctx);
    return slot(h, index, bone);
}

static int a_bone(void *ctx, uint32_t bone, uint32_t offset, float value)
{
    AD(ctx);
    return wr(h, bone + offset, 4, fbits(value));
}

static int a_001A2370(void *ctx, uint32_t matrix)
{
    AD(ctx);
    const uint8_t *m;
    BEFORE();
    LD(matrix, 64, &m);
    if (!h->w->w_001A2370) return hf_fault(h->fault, 0x001A2370u, EM_GUN_FAULT_NULL);
    if (h->w->w_001A2370(h->w->ctx, h->a, m) < 0) return -1;
    AFTER();
    return 0;
}

static int a_001AFA90(void *ctx, uint8_t cls, uint32_t *node, EmGunLamp **view)
{
    AD(ctx);
    CALLA(0x001AFA90u, w_001AFA90, cls, node);
    *view = NULL;
    if (*node) {
        d->child = *node;
        TRY(view_load(d));
        *view = &d->c;
    }
    return 0;
}

static int a_child(void *ctx, uint32_t child, EmGunLamp **view)
{
    AD(ctx);
    if (d->child != child) {
        BEFORE();
        d->child = child;
        TRY(view_load(d));
    }
    (void)h;
    *view = &d->c;
    return 0;
}

static int a_00102958(void *ctx, uint32_t dst, uint32_t src)
{
    AD(ctx);
    uint8_t *p;
    const uint8_t *q;
    BEFORE();
    ST(dst, 64, &p);
    LD(src, 64, &q);
    if (!h->w->w_00102958) return hf_fault(h->fault, 0x00102958u, EM_GUN_FAULT_NULL);
    if (h->w->w_00102958(h->w->ctx, p, q) < 0) return -1;
    AFTER();
    return 0;
}

static int delegate(Hf *h)
{
    Adapt d;
    memset(&d, 0, sizeof d);
    d.h = h;
    TRY(view_load(&d));
    uint32_t flag;
    RD8(0x00810758u + EM_GUN_FLAG_30, &flag);
    d.flags[EM_GUN_FLAG_30] = (uint8_t)flag;
    EmGunWorld world = {d.flags, NULL, NULL, &d.spad3A20};
    EmGunWorkers w;
    memset(&w, 0, sizeof w);
    w.ctx = &d;
    w.w_001C6380 = a_001C6380;
    w.w_001B17A0 = a_001B17A0;
    w.w_draw_4C = a_draw;
    w.w_001AFC10 = a_001AFC10;
    w.w_001B0FD0 = a_001B0FD0;
    w.w_00122BB8 = a_00122BB8;
    w.w_0011E2A8 = a_0011E2A8;
    w.r_00275B40 = a_00275B40;
    w.s_bone_f32 = a_bone;
    w.w_001A2370 = a_001A2370;
    w.w_001AFA90 = a_001AFA90;
    w.r_child_220 = a_child;
    w.w_00102958 = a_00102958;
    int r = em_gun_tick(&d.v, &world, &w, h->fault);
    if (r < 0) return -1;
    if (r > 0) TRY(view_flush(&d));
    return r;
}

/* ---- entry points ----------------------------------------------------------- */

int em_gun_rest_tick(uint32_t actor, const EmGunRestMem *mem,
                              const EmGunRestWorkers *w, EmGunFault *fault)
{
    if (!fault || fault->code != EM_GUN_FAULT_NONE) return -1;
    if (!mem || !w) return hf_fault(fault, EM_GUN, EM_GUN_FAULT_NULL);
    Hf hf = {mem, w, fault, actor};
    Hf *h = &hf;
    uint32_t lifecycle;
    RD8(actor + 4, &lifecycle);
    if (lifecycle == 4) return lifecycle4(h);
    if (lifecycle == 1) return lifecycle1(h);
    return delegate(h);
}

/* ---- 0021AAC0: the cable's hit effect node --------------------------------- */

#define FX_TABLE_A 0x002669C0u   /* 001CFBE0 a2, kind 2 */
#define FX_TABLE_B 0x00266A50u   /* 001CFBE0 a2, kind 1 */
#define K_HALF_PI UINT32_C(0x3FC90FDB)
#define K_2POW31 UINT32_C(0x4F000000)
#define K_EIGHT UINT32_C(0x41000000)
#define K_RISE UINT32_C(0x3CA3D70A)     /* 0.02 */
#define K_48 UINT32_C(0x42400000)
#define K_HALF UINT32_C(0x3F000000)

/* 0x700036A0 = identity rotated by pi/2 (00102B08) and translated by v. */
static int fx_matrix(Hf *h, const uint8_t *v)
{
    uint8_t *m;
    const uint8_t *src;
    ST(SP(0x36A0), 64, &m);
    W(0x001029C0u, w_001029C0, m);
    ST(SP(0x36A0), 64, &m);
    LD(SP(0x36A0), 64, &src);
    W(0x00102B08u, w_00102B08, m, src, K_HALF_PI);
    ST(SP(0x36A0), 64, &m);
    LD(SP(0x36A0), 64, &src);
    W(0x00102918u, w_00102918, m, src, v);
    return 0;
}

int em_gun_rest_0021AAC0(uint32_t node, const EmGunRestMem *mem, const EmGunRestWorkers *w,
                         EmGunFault *fault)
{
    if (!fault || fault->code != EM_GUN_FAULT_NONE) return -1;
    if (!mem || !w) return hf_fault(fault, 0x0021AAC0u, EM_GUN_FAULT_NULL);
    Hf hf = {mem, w, fault, node};
    Hf *h = &hf;
    const uint32_t e = node + 0x1F0;
    uint32_t parent, st, count, points;
    uint8_t *dst;
    const uint8_t *a, *b;
    RD32(node + 0x24, &parent);
    RD8(node + 4, &st);
    if (st == 3 || st == 2) {                                 /* 0x21AE54 */
        W(0x001AFC10u, w_001AFC10, node);
        return 0;
    }
    if (st != 1 && st != 0) return 1;
    if (st == 0) {
        for (uint32_t i = 0; i < 6; ++i) {                    /* 0x21AB2C */
            int32_t r;
            W(0x00122BB8u, w_00122BB8, &r);
            WR32(e + 0x78 + 4 * i, em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)r), K_2POW31));
            WR32(e + 0x60 + 4 * i, 0);
        }
        WR32(SP(0x38A0), 0);
        WR32(SP(0x38A4), 0);
        WR32(SP(0x38A8), K_ONE);
        WR32(SP(0x38AC), K_ONE);
        ST(node + 0xD0, 64, &dst);
        LD(parent + 0xD0, 64, &a);
        W(0x00102958u, w_00102958, dst, a);                   /* 0x21AB8C */
        ST(node + 0xB0, 16, &dst);
        LD(node + 0xD0, 64, &a);
        LD(SP(0x38A0), 16, &b);
        W(0x001026A0u, w_001026A0, dst, a, b);                /* 0x21ABA0 */
        WR32(e + 0x98, 0);
        WR32(e + 0x94, 0);
        WR32(e + 0x90, 0);
        WR8(node + 4, 1);
    }
    RD32(e + 0x98, &count);                                   /* 0x21ABBC */
    WR32(e + 0x98, count + 1u);
    RD32(e + 0x98, &count);
    if ((int32_t)count >= 0x1F) {
        RD32(e + 0x94, &points);
        if ((int32_t)count % 6 == 1 && (int32_t)points < 6) { /* 0x21ABF0 */
            uint32_t k, y;
            RD32(e + 0x94, &k);
            WR32(e + k * 16, 0);
            RD32(e + 0x94, &k);
            RD32(e + 0x90, &y);
            WR32(e + k * 16 + 4, y);
            RD32(e + 0x94, &k);
            WR32(e + k * 16 + 8, 0);
            RD32(e + 0x94, &k);
            WR32(e + k * 16 + 0xC, K_ONE);
            RD32(e + 0x94, &k);
            ST(e + k * 16, 16, &dst);
            LD(e + k * 16, 16, &a);
            LD(node + 0xB0, 16, &b);
            W(0x001028B8u, w_001028B8, dst, a, b);            /* 0x21AC5C */
            RD32(e + 0x90, &y);
            WR32(e + 0x90, em_ee_sub_bits(y, K_EIGHT));
            RD32(e + 0x94, &k);
            WR32(e + 0x94, k + 1u);
        }
        for (uint32_t n = 0;; ++n) {                          /* 0x21AD60 */
            RD32(e + 0x94, &points);
            if (!((int32_t)n < (int32_t)points)) break;
            uint8_t block[0x60];
            const uint8_t *p;
            int32_t key;
            uint32_t phase, scale;
            LD(e + n * 16, 16, &p);
            TRY(fx_matrix(h, p));
            LD(SP(0x36D0), 16, &p);
            W(0x001CCF70u, w_001CCF70, p, &key);              /* 0x21ACE0 */
            RD32(e + 0x60 + 4 * n, &phase);
            RD32(e + 0x78 + 4 * n, &scale);
            LD(SP(0x36A0), 64, &a);
            W(0x001CFA60u, w_001CFA60, block, a, phase, scale); /* 0x21ACFC */
            W(0x001CFBE0u, w_001CFBE0, key, 2, FX_TABLE_A, block, 0);
            W(0x001CFBE0u, w_001CFBE0, key, 1, FX_TABLE_B, block, 0);
            RD32(e + 0x60 + 4 * n, &phase);
            WR32(e + 0x60 + 4 * n, em_ee_add_bits(phase, K_RISE));
        }
    }
    RD32(e + 0x98, &count);                                   /* 0x21AD70 */
    if ((int32_t)count < 0x3C && (int32_t)count % 10 == 1) {
        uint32_t child;
        const uint8_t *p;
        LD(node + 0xB0, 16, &p);
        TRY(fx_matrix(h, p));
        LD(SP(0x36A0), 64, &a);
        W(0x001EFEB0u, w_001EFEB0, UINT32_C(0x8000003B), a, &child); /* 0x21ADF0 */
        if (child != 0) {
            WR8(child + 5, 0);
            WR32(child + 0x1F0, 0xC);
            WR32(child + 0x1F4, K_48);
            WR32(child + 0x1F8, K_HALF);
        }
    }
    RD32(e + 0x98, &count);
    if (count == 0x3C) WR8(parent + 4, 3);                    /* 0x21AE30 */
    RD32(e + 0x98, &count);
    if ((int32_t)count >= 0x79) WR8(node + 4, 3);             /* 0x21AE4C */
    return 1;
}

/* ---- 0021A500: the strip node ------------------------------------------------ */

#define K_TWO_PI UINT32_C(0x40C90FDB)
#define K_2_5 UINT32_C(0x40200000)
#define K_0_2 UINT32_C(0x3E4CCCCD)
#define K_192 UINT32_C(0x43400000)
#define K_1E_4 UINT32_C(0x38D1B717)
#define K_65535 UINT32_C(0x477FFF00)
#define K_1E_6 UINT32_C(0x358637BD)
#define K_1_1 UINT32_C(0x3F8CCCCD)
#define K_0_04 UINT32_C(0x3D23D70A)
#define K_SEVEN UINT32_C(0x40E00000)
#define K_NEG_ONE UINT32_C(0xBF800000)
#define STRIP_POINTS 0x00821400u             /* D_00821400 */
#define STRIP_TABLE 0x00266930u              /* 001CFBE0 a2 */
#define K_STRIP_TAG UINT64_C(0x20045D8555422188) /* 001CE860 t1 */

/* 2.5 * sinf(2 pi * rand / 2^31) (0x21A594..0x21A5DC). */
static int strip_offset(Hf *h, uint32_t *out)
{
    int32_t r;
    uint32_t s;
    W(0x00122BB8u, w_00122BB8, &r);
    uint32_t frac = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)r), K_2POW31);
    W(0x0011E2A8u, w_0011E2A8, em_ee_mul_bits(K_TWO_PI, frac), &s);
    *out = em_ee_mul_bits(K_2_5, s);
    return 0;
}

int em_gun_rest_0021A500(uint32_t node, const EmGunRestMem *mem, const EmGunRestWorkers *w,
                         EmGunFault *fault)
{
    if (!fault || fault->code != EM_GUN_FAULT_NONE) return -1;
    if (!mem || !w) return hf_fault(fault, 0x0021A500u, EM_GUN_FAULT_NULL);
    Hf hf = {mem, w, fault, node};
    Hf *h = &hf;
    const uint32_t e = node + 0x1F0;
    uint32_t st, n, v, f, x;
    uint8_t *dst;
    const uint8_t *a, *b;
    RD8(node + 4, &st);
    if (st == 3 || st == 2) {                                 /* 0x21AA88 */
        W(0x001AFC10u, w_001AFC10, node);
        return 0;
    }
    if (st != 1 && st != 0) return 1;
    if (st == 0) {                                            /* 0x21A564 */
        WR32(e + 0x18, 0);
        WR32(e + 0x48, 0);
        RD32(e, &n);
        WR32(e + 0x14 + 4 * n, 0);
        RD32(e, &n);
        WR32(e + 0x44 + 4 * n, 0);
        for (uint32_t i = 1;; ++i) {
            RD32(e, &n);
            if (!((int32_t)i < (int32_t)(n - 1u))) break;
            TRY(strip_offset(h, &x));
            WR32(e + 0x18 + 4 * i, x);
            TRY(strip_offset(h, &x));
            WR32(e + 0x48 + 4 * i, x);
        }
        int32_t r;
        W(0x00122BB8u, w_00122BB8, &r);
        WR32(e + 0xC, (uint32_t)r);
        WR32(e + 0x10, 0);
        WR32(e + 0x14, 0);
        RD32(e, &n);
        RD32(e + 4, &f);
        WR32(e + 4, em_ee_div_bits(f, em_ee_cvt_s_w_bits(n - 1u)));
        WR8(node + 4, 1);
    }
    /* 0x21A688: the points, through the node's +0xD0 matrix. */
    for (uint32_t i = 0;; ++i) {
        RD32(e, &n);
        if (!((int32_t)i < (int32_t)n)) break;
        uint32_t grow, width, off, len;
        RD32(e + 0x10, &grow);
        RD32(e + 8, &width);
        RD32(e + 0x18 + 4 * i, &off);
        x = em_ee_mul_bits(off, em_ee_mul_bits(em_ee_add_bits(K_0_2, grow),
                                               em_ee_mul_bits(K_TWO, width)));
        WR32(SP(0x38A0), x);
        RD32(e + 8, &width);
        RD32(e + 0x10, &grow);
        RD32(e + 0x48 + 4 * i, &off);
        x = em_ee_mul_bits(off, em_ee_mul_bits(em_ee_add_bits(K_0_2, grow),
                                               em_ee_mul_bits(K_TWO, width)));
        WR32(SP(0x38A4), x);
        RD32(e + 4, &len);
        RD32(e + 0x10, &grow);
        WR32(SP(0x38A8), em_ee_add_bits(grow, em_ee_mul_bits(len, em_ee_cvt_s_w_bits(i))));
        WR32(SP(0x38AC), K_ONE);
        ST(STRIP_POINTS + 16 * i, 16, &dst);
        LD(node + 0xD0, 64, &a);
        LD(SP(0x38A0), 16, &b);
        W(0x001026A0u, w_001026A0, dst, a, b);                /* 0x21A730 */
        WR32(STRIP_POINTS + 16 * i + 0xC, K_ONE);
    }
    for (uint32_t k = 0; k < 3; ++k) {                        /* 0x21A760..0x21A7D8 */
        RD32(e + 0x10, &f);
        WR32(SP(0x38B0 + 4 * k), em_ee_mul_bits(K_192, em_ee_sub_bits(K_ONE, f)));
    }
    {
        uint32_t width;
        RD32(e, &n);
        RD32(e + 8, &width);
        LD(STRIP_POINTS, 16 * ((int32_t)n > 0 ? n : 1u), &a);
        LD(SP(0x38B0), 16, &b);
        W(0x001CE860u, w_001CE860, 0, 2, a, b, width, (int32_t)n, K_STRIP_TAG); /* 0x21A7E4 */
    }
    RD32(e + 0x10, &f);                                       /* 0x21A7EC */
    x = em_ee_add_bits(f, em_ee_div_bits(em_ee_sub_bits(K_ONE, f), K_EIGHT));
    WR32(e + 0x10, x);
    WR32(e + 0x10, em_ee_add_bits(x, K_1E_4));
    RD8(node + 5, &v);
    if (v == 0) {
        RD32(e + 0x10, &f);
        if (!em_ee_c_le_bits(f, K_ONE)) WR8(node + 4, 3);     /* 0x21A84C */
        return 1;
    }
    if (v != 1) return 1;
    /* 0x21A860: two end sprites, from a 0x80-byte local of two matrices. */
    uint8_t local[0x80], block[0x60];
    uint32_t colour;
    RD32(e + 0xC, &colour);
    LD(node + 0xD0, 64, &a);
    W(0x00102958u, w_00102958, local, a);                     /* copy_qw4 */
    LD(STRIP_POINTS, 16, &a);
    W(0x00102948u, w_00102948, local + 0x30, a);
    for (uint32_t k = 0; k < 3; ++k) {
        LD(node + 0xD0 + 16 * k, 16, &a);
        W(0x00103230u, w_00103230, local + 0x40 + 16 * k, a, K_NEG_ONE);
    }
    RD32(e, &n);
    LD(STRIP_POINTS + (n - 1u) * 16u, 16, &a);
    W(0x00102948u, w_00102948, local + 0x70, a);
    for (uint32_t k = 0; k < 2; ++k) {                        /* 0x21A8E4 */
        uint8_t *m = local + 0x40 * k;
        int32_t key, t;
        uint32_t spent;
        W(0x001CCF70u, w_001CCF70, m + 0x30, &key);
        uint32_t frac = em_ee_add_bits(
            em_ee_div_bits(em_ee_cvt_s_w_bits(sra32(colour, 16) & 0xFFFFu), K_65535), K_1E_4);
        colour = colour * 37u + 0xBu;
        RD32(e + 0x14, &spent);
        W(0x001CFB50u, w_001CFB50, block, 0, m, spent, frac, K_ONE, K_1E_6, K_FIVE);
        W(0x001CFBE0u, w_001CFBE0, key, 1, STRIP_TABLE, block, 0);
        RD32(e + 0x14, &spent);
        uint32_t fade = em_ee_sub_bits(K_ONE, em_ee_div_bits(spent, K_1_1));
        uint32_t f128 = em_ee_mul_bits(K_128, fade);
        WR32(SP(0x3A20), fade);
        uint32_t c;
        W(0x001281C0u, w_001281C0, f128, &t);
        c = (uint32_t)t;
        W(0x001281C0u, w_001281C0, f128, &t);
        c |= (uint32_t)t << 8;
        W(0x001281C0u, w_001281C0, em_ee_mul_bits(K_192, fade), &t);
        c |= (uint32_t)t << 16;
        W(0x001CD520u, w_001CD520, 0, 2, m + 0x30, K_SPRITE_TAG, c, K_SEVEN, K_SEVEN, K_FIVE);
    }
    RD32(e + 0x14, &f);                                       /* 0x21AA44 */
    f = em_ee_add_bits(f, K_0_04);
    WR32(e + 0x14, f);
    if (!em_ee_c_le_bits(f, K_1_1)) WR8(node + 4, 3);
    return 1;
}

/* ---- 001EFEB0 ------------------------------------------------------------------ */

int em_gun_rest_001EFEB0(uint32_t id, const uint8_t *m, const EmGunRestMem *mem,
                         const EmGunRestWorkers *w, uint32_t *node, EmGunFault *fault)
{
    if (!fault || fault->code != EM_GUN_FAULT_NONE) return -1;
    if (!mem || !w || !m || !node) return hf_fault(fault, 0x001EFEB0u, EM_GUN_FAULT_NULL);
    Hf hf = {mem, w, fault, 0};
    Hf *h = &hf;
    W(0x001EF9D0u, w_001EF9D0, id, m + 0x30, K_ONE, node);
    if (*node == 0) return 0;
    uint8_t *dst;
    ST(*node + 0xD0, 64, &dst);
    W(0x00102958u, w_00102958, dst, m);
    return 0;
}

int em_gun_rest_001B1190(int32_t a0, const EmGunRestMem *mem, EmGunFault *fault)
{
    if (!fault || fault->code != EM_GUN_FAULT_NONE) return -1;
    if (!mem) return hf_fault(fault, 0x001B1190u, EM_GUN_FAULT_NULL);
    Hf hf = {mem, NULL, fault, 0};
    Hf *h = &hf;
    uint32_t id = (uint32_t)a0 & 0xFFu, area, word;
    if (id == 0) return 0;
    RD8(0x00810700u, &area);
    uint32_t address = 0x00810860u + (area << 5) + (id >> 5) * 4u;
    RD32(address, &word);
    WR32(address, word | (UINT32_C(1) << ((uint32_t)a0 & 0x1Fu)));
    return 0;
}
