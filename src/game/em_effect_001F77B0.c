/* em_effect_001F77B0.c - the effect node behaviour 001F77B0 (see
 * em_effect_001F77B0.h, docs/DAMAGE.md section 4). Translated from the
 * instructions; the comments cite the original addresses of each step. */
#include "game/em_effect_001F77B0.h"

#include "game/em_ee_float.h"

typedef uint32_t u32;

#define F_ONE 0x3F800000u
#define F_1_5 0x3FC00000u
#define F_2POW31 0x4F000000u
#define F_TWO_PI 0x40C90FDBu
#define F_PI 0x40490FDBu
#define TEX0_BLOOD UINT64_C(0x2004108555322080)

static u32 rd(const uint8_t *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void wr(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint64_t rd64(const uint8_t *p) { return (uint64_t)rd(p) | (uint64_t)rd(p + 4) << 32; }
static void wr64(uint8_t *p, uint64_t v)
{
    wr(p, (u32)v);
    wr(p + 4, (u32)(v >> 32));
}

static int fail(uint32_t *fault, u32 address)
{
    if (fault && !*fault) *fault = address;
    return -1;
}
#define CALL(address, expr) do { if ((expr) < 0) return fail(fault, (address)); } while (0)

/* State 0's parameter block by the subtype (0x1F7814..0x1F797C). */
static void parameters(uint8_t *w, uint8_t subtype)
{
    u32 a4, a8, b8, bc, c0, ac;
    switch (subtype) {
    case 2: a4 = 4; a8 = 0; b8 = 0x43520000u; bc = 0x40E00000u; c0 = 0x40000000u; ac = 0x80020220u; break;
    case 3: a4 = 8; a8 = 4; b8 = F_ONE; bc = 0x40E00000u; c0 = 0x40A00000u; ac = 0x80200220u; break;
    case 4: case 5: a4 = 8; a8 = 8; b8 = F_ONE; bc = 0x41700000u; c0 = 0x41200000u; ac = 0x80200220u; break;
    default: return;
    }
    wr(w + 0xA4, a4);
    wr(w + 0xA8, a8);
    wr(w + 0xB8, b8);
    wr(w + 0xBC, bc);
    wr(w + 0xC0, c0);
    wr64(w + 0xB0, TEX0_BLOOD);
    wr(w + 0xAC, ac);
}

int em_effect_001F77B0(const EmEffect001F77B0Node *n, const EmEffect001F77B0Workers *w, uint32_t *fault)
{
    if (fault) *fault = 0;
    if (!n || !w || !n->state || !n->b0 || !n->c0 || !n->work || !n->spad3A20 || !n->spad3A24)
        return fail(fault, EM_EFFECT_001F77B0_CALLBACK);
    uint8_t *k = n->work;
    const uint8_t st = *n->state;
    if (st == 3 || st == 2) {                                         /* 0x1F77E4 / 0x1F77F0 */
        if (!w->w_001AFC10) return fail(fault, 0x001AFC10u);
        CALL(0x001AFC10u, w->w_001AFC10(w->ctx));
        return 0;
    }
    if (st != 0 && st != 1) return 0;                                 /* 0x1F780C */
    if (!w->w_00122BB8 || !w->w_0011E2A8 || !w->w_0011DE90 || !w->w_001CE300 || !w->r_00810360)
        return fail(fault, EM_EFFECT_001F77B0_CALLBACK);
    if (st == 0) {
        parameters(k, n->subtype);                                    /* 0x1F7814 */
        uint8_t *p = k;                                               /* s3: the offsets */
        uint8_t *size = k;                                            /* s4: the sizes */
        for (int32_t i = 0; i < (int32_t)rd(k + 0xA4); ++i) {        /* 0x1F7A60 */
            int32_t r;
            CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));            /* 0x1F7990 */
            u32 f1 = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)r), F_2POW31);   /* 0x1F79C4 */
            f1 = em_ee_mul_bits(F_TWO_PI, f1);                        /* 0x1F79C8 */
            *n->spad3A20 = em_ee_sub_bits(f1, F_PI);                  /* 0x1F79D4, 0x1F79DC */
            CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));            /* 0x1F79D8 */
            const u32 angle = *n->spad3A20;                           /* 0x1F79F0 */
            *n->spad3A24 = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)r), F_2POW31);   /* 0x1F79FC, 0x1F7A0C */
            u32 s;
            CALL(0x0011E2A8u, w->w_0011E2A8(w->ctx, angle, &s));     /* 0x1F7A08 */
            u32 f0 = em_ee_mul_bits(rd(k + 0xC0), s);                 /* 0x1F7A1C */
            wr(p + 0, em_ee_mul_bits(*n->spad3A24, f0));              /* 0x1F7A24, 0x1F7A28 */
            u32 c;
            CALL(0x0011DE90u, w->w_0011DE90(w->ctx, *n->spad3A20, &c));   /* 0x1F7A2C */
            f0 = em_ee_mul_bits(rd(k + 0xC0), c);                     /* 0x1F7A44 */
            wr(p + 8, em_ee_mul_bits(*n->spad3A24, f0));              /* 0x1F7A48, 0x1F7A4C */
            wr(size + 0x80, 0);                                       /* 0x1F7A50 */
            p += 0x10;
            size += 4;
        }
        int32_t r;
        CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));                /* 0x1F7A70 */
        wr(k + 0xA0, (u32)r);                                         /* 0x1F7A7C */
        *n->state = 1;                                                /* 0x1F7A80 */
    }
    /* 0x1F7A84: the offset from the spawn point to the player's +0xB0. */
    u32 listener[3];
    CALL(0x00810360u, w->r_00810360(w->ctx, listener));
    const u32 dx = em_ee_sub_bits(listener[0], n->c0[0]);             /* 0x1F7AA8 */
    const u32 dy = em_ee_sub_bits(listener[1], n->c0[1]);             /* 0x1F7AB8 */
    const u32 dz = em_ee_sub_bits(listener[2], n->c0[2]);             /* 0x1F7AC0 */
    const uint8_t *p = k;                                             /* s1 */
    uint8_t *size = k;                                                /* s3 */
    for (int32_t i = 0; i < (int32_t)rd(k + 0xA4); ++i) {            /* 0x1F7CA0 */
        u32 quad[16];
        const u32 lift = em_ee_sub_bits(dy, F_1_5);                   /* 0x1F7ADC */
        for (int corner = 0; corner < 4; ++corner) {                  /* 0x1F7AEC */
            u32 *v = quad + 4 * corner;
            v[0] = em_ee_add_bits(n->b0[0], rd(p + 0));              /* 0x1F7AF4 */
            v[1] = n->b0[1];                                          /* 0x1F7AFC */
            v[2] = em_ee_add_bits(n->b0[2], rd(p + 8));              /* 0x1F7B0C */
            v[3] = F_ONE;                                             /* 0x1F7B14 */
            if (n->subtype == 2) {                                    /* 0x1F7B1C */
                v[0] = em_ee_add_bits(v[0], dx);
                v[1] = em_ee_add_bits(v[1], lift);
                v[2] = em_ee_add_bits(v[2], dz);
            }
            const u32 s = em_ee_mul_bits(rd(k + 0xBC), rd(size + 0x80));
            switch (corner) {
            case 0: v[0] = em_ee_sub_bits(v[0], s); v[2] = em_ee_sub_bits(v[2], s); break;   /* 0x1F7B70 */
            case 1: v[0] = em_ee_add_bits(v[0], s); v[2] = em_ee_sub_bits(v[2], s); break;   /* 0x1F7BA4 */
            case 2: v[0] = em_ee_sub_bits(v[0], s); v[2] = em_ee_add_bits(v[2], s); break;   /* 0x1F7BD8 */
            default: v[0] = em_ee_add_bits(v[0], s); v[2] = em_ee_add_bits(v[2], s); break;  /* 0x1F7C0C */
            }
        }
        CALL(0x001CE300u, w->w_001CE300(w->ctx, 1, quad, rd64(k + 0xB0), rd(k + 0xAC)));   /* 0x1F7C5C */
        const u32 grown = em_ee_add_bits(rd(size + 0x80), em_ee_div_bits(F_ONE, rd(k + 0xB8)));   /* 0x1F7C74 */
        wr(size + 0x80, grown);                                       /* 0x1F7C88 */
        if (!em_ee_c_le_bits(grown, F_ONE)) wr(size + 0x80, F_ONE);  /* 0x1F7C7C, 0x1F7C8C */
        p += 0x10;
        size += 4;
    }
    return 0;
}
