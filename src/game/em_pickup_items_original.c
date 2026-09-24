/* em_pickup_items_original.c - 001C40B0, 001F1110 and 001F1180 (see the
 * header). Each routine follows the original's .s: the order of its loads
 * and stores, the widths it stores in and the values its compares reload. */
#include "game/em_pickup_items_original.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

/* ---- 001C40B0 ------------------------------------------------------------ */

enum {
    C62 = 0x00810C62u, C63 = 0x00810C63u, C64 = 0x00810C64u, C70 = 0x00810C70u,
    C71 = 0x00810C71u, C72 = 0x00810C72u, CA8 = 0x00810CA8u, CAA = 0x00810CAAu,
    CAC = 0x00810CACu, CAE = 0x00810CAEu, CB0 = 0x00810CB0u, CB2 = 0x00810CB2u,
    CB4 = 0x00810CB4u, CB7 = 0x00810CB7u
};

typedef struct {
    EmPickupItemsAt at;
    void *ctx;
    int failed;
} Items;

static uint8_t *byte_at(Items *m, uint32_t address)
{
    uint8_t *p = m->failed ? NULL : m->at(m->ctx, address, 1);
    if (!p) m->failed = 1;
    return p;
}

static uint8_t *half_at(Items *m, uint32_t address)
{
    uint8_t *p = m->failed ? NULL : m->at(m->ctx, address, 2);
    if (!p) m->failed = 1;
    return p;
}

/* lbu / sb */
static uint32_t lbu(Items *m, uint32_t address)
{
    uint8_t *p = byte_at(m, address);
    return p ? *p : 0;
}

static void sb(Items *m, uint32_t address, uint32_t value)
{
    uint8_t *p = byte_at(m, address);
    if (p) *p = (uint8_t)value;
}

/* lh (sign-extended) / sh */
static int32_t lh(Items *m, uint32_t address)
{
    uint8_t *p = half_at(m, address);
    return p ? (int32_t)(int16_t)(uint16_t)(p[0] | p[1] << 8) : 0;
}

static void sh(Items *m, uint32_t address, uint32_t value)
{
    uint8_t *p = half_at(m, address);
    if (p) {
        p[0] = (uint8_t)value;
        p[1] = (uint8_t)(value >> 8);
    }
}

/* count += a1 (the byte stored wraps). */
static void count_add(Items *m, uint32_t count, int32_t a1)
{
    sb(m, count, lbu(m, count) + (uint32_t)a1);
}

/* meter += delta, then the reloaded meter >= 100 -> 99. */
static int meter_add(Items *m, uint32_t meter, uint32_t delta)
{
    sh(m, meter, (uint32_t)lh(m, meter) + delta);
    if (lh(m, meter) < 0x64) return 0;
    sh(m, meter, 0x63);
    return 1;
}

/* The battery cases 0x1B/0x1C/0x1D: count += a1, D_00810CB2 += a1 * unit,
 * D_00810CB7 raised to `unit`, then D_00810CB2 capped at D_00810CB7. */
static void battery(Items *m, uint32_t count, int32_t a1, uint32_t unit)
{
    count_add(m, count, a1);
    sh(m, CB2, (uint32_t)lh(m, CB2) + (uint32_t)a1 * unit);
    if (lbu(m, CB7) < unit) sb(m, CB7, unit);
    int32_t charge = lh(m, CB2);
    uint32_t capacity = lbu(m, CB7);
    if ((int32_t)capacity < charge) sh(m, CB2, capacity & 0xFF);
}

int em_pickup_items_001C40B0(EmPickupItemsAt at, void *ctx, int32_t a0, int32_t a1)
{
    if (!at) return -1;
    Items m = {at, ctx, 0};
    const uint32_t count = C64 + (uint32_t)a0;
    const uint32_t n = (uint32_t)a1;
    switch (a0) {
    case 0xE:
    case 0xD:
    case 0xC:
        count_add(&m, count, a1);
        if (lbu(&m, C70) && lbu(&m, C71) && lbu(&m, C72)) (void)meter_add(&m, CB0, 1);
        break;
    case 4:
        count_add(&m, count, a1);
        if (meter_add(&m, CAC, 1)) sh(&m, CAE, 0);
        break;
    case 3:
    case 2:
        count_add(&m, count, a1);
        (void)meter_add(&m, CAA, 6);
        break;
    case 1:
        count_add(&m, count, a1);
        (void)meter_add(&m, CA8, 6);
        break;
    case 0xF:
        sb(&m, count, n);
        break;
    case 0x1D:
        battery(&m, count, a1, 0x30);
        break;
    case 0x1C:
        battery(&m, count, a1, 0x24);
        break;
    case 0x1B:
        battery(&m, count, a1, 0xC);
        break;
    case 0x16:
        (void)meter_add(&m, CB0, n);
        break;
    case 0x15:
        if (meter_add(&m, CAC, n)) sh(&m, CAE, 0);
        break;
    case 0x14:
        (void)meter_add(&m, CAA, n * 0xC);
        break;
    case 0x13:
        (void)meter_add(&m, CAA, n * 6);
        break;
    case 0x12:
        (void)meter_add(&m, CA8, n * 0xC);
        break;
    case 0x11:
        (void)meter_add(&m, CA8, n * 6);
        break;
    case 0x10:
        /* The magazine pack: count, D_00810C63 and D_00810CB4 (+30 a pack),
         * D_00810C62 = 30 when empty, then the reloaded pack byte >= 99
         * gives the surplus packs' rounds back and pins both bytes at 98. */
        count_add(&m, count, a1);
        sb(&m, C63, lbu(&m, C63) + n);
        sh(&m, CB4, (uint32_t)lh(&m, CB4) + n * 0x1E);
        if (!lbu(&m, C62)) sb(&m, C62, 0x1E);
        {
            uint32_t packs = lbu(&m, C63);
            if (packs >= 0x63) {
                sh(&m, CB4, (uint32_t)lh(&m, CB4) - ((packs & 0xFF) - 0x62) * 0x1E);
                sb(&m, C63, 0x62);
                sb(&m, count, 0x62);
            }
        }
        break;
    default:
        count_add(&m, count, a1);
        if (lbu(&m, count) >= 0x64) sb(&m, count, 0x63);
        break;
    }
    return m.failed ? -1 : 0;
}

/* ---- 001F1110 / 001F1180 ------------------------------------------------- */

enum {
    F_ZERO = 0x00000000u, F_ONE = 0x3F800000u, F_SIX = 0x40C00000u, F_180 = 0x43340000u,
    F_360 = 0x43B40000u, F_40 = 0x42200000u, F_STEP = 0x3D4CCCCDu /* 0.05 */
};

/* rand() % m with the EE div's signed remainder (mfhi). */
static int rand_mod(const EmPickupAuraWorkers *w, int32_t modulus, int32_t *out)
{
    if (!w || !w->w_00122BB8) return -1;
    int32_t r = w->w_00122BB8(w->ctx);
    *out = r % modulus;
    return 0;
}

/* 40.0 + (float)(rand() % 120): the cvt.s.w and add.s of 001F1110 and of
 * 001F1180's re-arm. */
static int countdown(const EmPickupAuraWorkers *w, uint32_t *timer)
{
    int32_t rem;
    if (rand_mod(w, 0x78, &rem) < 0) return -1;
    *timer = em_ee_add_bits(F_40, em_ee_cvt_s_w_bits((uint32_t)rem));
    return 0;
}

int em_pickup_aura_001F1110(EmPickupAura *a, int16_t variant, const EmPickupAuraWorkers *w)
{
    if (!a) return -1;
    uint32_t timer;
    if (countdown(w, &timer) < 0) return -1;
    a->timer = timer;
    a->angle = F_ZERO;
    a->variant = variant;
    a->index = 0;
    a->state = 0;
    return 0;
}

/* The SDK vector leaves on the VU0 model (their macro-op forms). */
/* 001028D0(out, a, b): out = a - b in all four lanes. */
static int vsub4(float out[4], const float a[4], const float b[4])
{
    return em_vu_vec(EM_VU_SUB, 0xF, EM_VU_NO_BC, a, b, 0.0f, NULL, out) ? -1 : 0;
}

/* 00102738(a, b): the xyz products summed into the x lane. */
static int vdot(const float a[4], const float b[4], float *out)
{
    float v[4];
    memcpy(v, b, sizeof v);
    if (em_vu_vec(EM_VU_MUL, 0xE, EM_VU_NO_BC, a, v, 0.0f, NULL, v)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 1, v, v, 0.0f, NULL, v)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 2, v, v, 0.0f, NULL, v)) return -1;
    *out = v[0];
    return 0;
}

/* 00102760(out, v): the squared length (the xyz products summed into x),
 * its VU square root added to a zero lane, the reciprocal of that (VU
 * division of 1.0 by it), then out = (v.xyz times the reciprocal, 0). */
static int vnormalize(float out[4], const float v[4])
{
    static const float vf0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float s[4], r[4], q;
    memcpy(s, v, sizeof s);
    if (em_vu_vec(EM_VU_MUL, 0xE, EM_VU_NO_BC, v, v, 0.0f, NULL, s)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 1, s, s, 0.0f, NULL, s)) return -1;
    if (em_vu_vec(EM_VU_ADDBC, 0x8, 2, s, s, 0.0f, NULL, s)) return -1;
    q = em_vu_sqrt(s[0]);
    if (em_vu_vec(EM_VU_ADDQ, 0x8, EM_VU_NO_BC, vf0, NULL, q, NULL, s)) return -1;
    if (em_vu_div(vf0[3], s[0], 3, 0, &q)) return -1;
    if (em_vu_vec(EM_VU_SUB, 0xF, EM_VU_NO_BC, vf0, vf0, 0.0f, NULL, r)) return -1;
    if (em_vu_vec(EM_VU_MULQ, 0xE, EM_VU_NO_BC, v, NULL, q, NULL, r)) return -1;
    memcpy(out, r, sizeof r);
    return 0;
}

int em_pickup_aura_001F1180(EmPickupAura *a, const float world[16], const float eye[4],
                            uint8_t d810700, const EmPickupAuraWorkers *w)
{
    if (!a || !w) return -1;
    if (a->state == 0) {
        /* The countdown (0x1F11C8..): timer -= 1.0, stored; below 0 it is
         * zeroed (before the rand() call) and the record index and state 1
         * are set. */
        uint32_t t = em_ee_sub_bits(a->timer, F_ONE);
        a->timer = t;
        if (!em_ee_c_lt_bits(t, F_ZERO)) return 0;
        a->timer = F_ZERO;
        int32_t rem;
        if (rand_mod(w, 4, &rem) < 0) return -1;
        a->index = (int16_t)rem;
        a->state = 1;
        return 0;
    }
    if (a->state != 1) return 0;

    /* The sprite record (0x1F121C..0x1F12DC). */
    uint32_t record;
    if (a->variant == 1) {
        uint32_t base = d810700 == 2 ? 0x0025A040u : d810700 == 1 ? 0x00259F90u : 0x00259EE0u;
        record = base + (uint32_t)(int32_t)a->index * 0x2Cu;
    } else {
        record = 0x00259DD0u + (uint32_t)(int32_t)a->variant * 0x2Cu;
    }

    /* Variants 4, 2 and 1 show only while the owner's +0xF0 row faces away
     * from the camera: dot(normalize(+0xF0), normalize(+0x100 - eye)) <= 0.
     * Otherwise the block returns to the countdown with its timer as is. */
    if (a->variant == 4 || a->variant == 2 || a->variant == 1) {
        if (!world || !eye) return -1;
        float toward[4], row[4], dot;
        if (vsub4(toward, world + 12, eye) < 0 || vnormalize(toward, toward) < 0 ||
            vnormalize(row, world + 8) < 0 || vdot(row, toward, &dot) < 0)
            return -1;
        if (!em_ee_c_le_bits(em_ee_bits(dot), F_ZERO)) {
            a->state = 0;
            return 0;
        }
    }

    if (!w->w_draw || w->w_draw(w->ctx, record, a->angle, a->timer) < 0) return -1;

    /* The angle ramp: += 6, past 180 wraps by 360 (reloaded). */
    uint32_t angle = em_ee_add_bits(a->angle, F_SIX);
    a->angle = angle;
    if (!em_ee_c_le_bits(angle, F_180)) a->angle = em_ee_sub_bits(a->angle, F_360);
    /* The phase ramp: += 0.05; past 1.0 a new countdown. */
    uint32_t t = em_ee_add_bits(a->timer, F_STEP);
    a->timer = t;
    if (em_ee_c_le_bits(t, F_ONE)) return 0;
    uint32_t timer;
    if (countdown(w, &timer) < 0) return -1;
    a->timer = timer;
    a->state = 0;
    return 0;
}
