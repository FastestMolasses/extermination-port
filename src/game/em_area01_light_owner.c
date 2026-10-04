/* Original owners 001C4FA0 / 001C50B0, byte-matched decomp C and checked
 * against original instructions by test_area01_light_owner_reference.py.
 * Addresses identify original calls; no disc data is embedded here. */
#include "game/em_area01_light_owner.h"

#include "game/em_ee_float.h"

static int done(const EmA01Math *m)
{
    return !m || em_a01m_faulted(m) ? -1 : 0;
}

int em_area01_light_001C4FA0(EmA01Math *m, uint32_t self, uint32_t *result)
{
    if (!m || em_a01m_faulted(m)) return -1;
    if (!result) {
        m->fault_code = EM_A01M_FAULT_NULL;
        m->fault_address = 0x001C4FA0u;
        return -1;
    }
    uint32_t value = 0;
    switch (em_a01m_lbu(m, self + 3u)) {
    case 1: value = em_a01m_lbu(m, 0x00810C87u) != 0; break;
    case 7: value = em_a01m_lbu(m, 0x0081075Du) != 0xFFu; break;
    case 5: value = em_a01m_lbu(m, 0x0081076Du) != 0; break;
    case 6: value = em_a01m_lbu(m, 0x00810770u) != 0; break;
    default: break;
    }
    if (done(m) < 0) return -1;
    *result = value;
    return 0;
}

static void color(EmA01Math *m, uint32_t self, uint32_t r, uint32_t g, uint32_t b)
{
    em_a01m_sw(m, self + 0x80u, r);
    em_a01m_sw(m, self + 0x84u, g);
    em_a01m_sw(m, self + 0x88u, b);
    em_a01m_sw(m, self + 0x8Cu, 0x3E800000u);
}

int em_area01_light_001C50B0(EmA01Math *m, uint32_t self, uint32_t sp)
{
    if (!m || em_a01m_faulted(m)) return -1;
    uint32_t result = 0;
    switch (em_a01m_lbu(m, self + 4u)) {
    case 0: {
        if (em_area01_light_001C4FA0(m, self, &result) < 0) return -1;
        if (result != 0) {
            em_a01m_sb(m, self + 4u, 3);
            break;
        }
        em_a01m_call_i(m, 0x001F5490u, 1, self, 0, 0, 0, &result);
        em_a01m_sb(m, self + 4u, result);
        if (em_a01m_lbu(m, self + 4u) == 3) break;
        uint32_t amplitude = 0;
        const uint32_t area = em_a01m_lbu(m, 0x00810700u);
        const uint32_t sub = em_a01m_lbu(m, 0x00810701u);
        const uint32_t mode = (area << 8) + sub;
        switch (mode) {
        case 0xF00:
            if (em_a01m_lbu(m, self + 0xDu) == 0) {
                color(m, self, 0x3F333333u, 0, 0);
                amplitude = 0x3FA00000u;
            } else {
                color(m, self, 0x3F800000u, 0x3F800000u, 0x3F800000u);
            }
            break;
        case 0x600: case 0x601:
            if (em_a01m_lbu(m, self + 0xDu) != 0) {
                color(m, self, 0, 0x3F800000u, 0);
            } else {
                color(m, self, 0, 0x3F000000u, 0);
                amplitude = 0x41133333u;
            }
            break;
        case 0x100:
            if (em_a01m_lbu(m, self + 0xDu) != 0) {
                color(m, self, 0, 0x3F800000u, 0);
            } else {
                color(m, self, 0x3F800000u, 0x3F800000u, 0x3F800000u);
                amplitude = 0x3F99999Au;
            }
            break;
        case 0x703:
            if (em_a01m_lbu(m, self + 0xDu) != 0) {
                color(m, self, 0, 0x3F800000u, 0);
            } else {
                color(m, self, 0, 0x3F000000u, 0);
                amplitude = 0x3F99999Au;
            }
            break;
        case 0x803:
            if (em_a01m_lbu(m, self + 0xDu) != 1) {
                color(m, self, 0, 0x3F800000u, 0);
            } else {
                color(m, self, 0, 0x3F000000u, 0);
                amplitude = 0x3F99999Au;
            }
            break;
        default:
            color(m, self, 0, 0x3F800000u, 0);
            break;
        }
        if (!em_ee_c_eq_bits(amplitude, 0)) {
            const int shift = (mode == 0x600 || mode == 0x601) &&
                              em_a01m_lbu(m, self + 0xDu) == 0;
            if (shift)
                em_a01m_call_i(m, 0x001028B8u, 3, self + 0xB0u, self + 0xB0u, 0x0026E2E0u, 0, NULL);
            em_a01m_call_if(m, 0x001C5050u, 1, self, 0, 0, 0, amplitude, &result, NULL);
            em_a01m_sw(m, self + 0x20u, result);
            if (shift)
                em_a01m_call_i(m, 0x001028D0u, 3, self + 0xB0u, self + 0xB0u, 0x0026E2E0u, 0, NULL);
        } else {
            em_a01m_sw(m, self + 0x20u, UINT32_MAX);
        }
        em_a01m_call_if(m, 0x00102900u, 2, self + 0x80u, self + 0x80u, 0, 0, 0x43000000u, NULL, NULL);
        break;
    }
    case 1: {
        if (em_area01_light_001C4FA0(m, self, &result) < 0) return -1;
        if (result != 0) {
            em_a01m_sb(m, self + 4u, 3);
            break;
        }
        const uint32_t positive = em_a01m_lw(m, self + 0x8Cu);
        const uint32_t negative = em_ee_neg_bits(positive);
        em_a01m_call_i(m, 0x00122BB8u, 0, 0, 0, 0, 0, &result);
        const uint32_t random = em_ee_mul_bits(0x30000000u, em_ee_cvt_s_w_bits(result));
        const uint32_t spread = em_ee_sub_bits(positive, negative);
        const uint32_t jitter = em_ee_add_bits(negative, em_ee_mul_bits(spread, random));
        const uint32_t local = sp - 0x10u;
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t v = em_ee_add_bits(em_a01m_lw(m, self + 0x80u + 4u * i), jitter);
            if (em_ee_c_lt_bits(v, 0)) v = 0;
            if (em_ee_c_lt_bits(0x437F0000u, v)) v = 0x437F0000u;
            em_a01m_sw(m, local + 4u * i, v);
        }
        em_a01m_sw(m, local + 0xCu, 0);
        const uint32_t model = em_a01m_lw(m, self + 0x44u);
        em_a01m_call_i(m, 0x001F5F60u, 4, self + 0xB0u, self + 0xC0u, local, model, NULL);
        break;
    }
    default:
        result = em_a01m_lw(m, self + 0x20u);
        if (result != UINT32_MAX)
            em_a01m_call_i(m, 0x001D80B0u, 1, result, 0, 0, 0, NULL);
        em_a01m_call_i(m, 0x001AFC10u, 1, self, 0, 0, 0, NULL);
        break;
    }
    return done(m);
}
