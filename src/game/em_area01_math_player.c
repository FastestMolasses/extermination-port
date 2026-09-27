/* em_area01_math_player.c - AREA01 lane "math": translations of 00183250,
 * 00187DE0 and 00187EC0 (em_area01_math_player.h, docs/AREA01_MATH.md).
 * 00183250 was read from the original instructions (asm-word body in the
 * decomp); 00187DE0 / 00187EC0 from their byte-matched C. Every COP1
 * operation goes through game/em_ee_float.h. Verified by
 * tools/test_area01_math_reference.py. */
#include "game/em_area01_math_player.h"

#include "game/em_ee_float.h"

#define F_001031E0 0x001031E0u
#define F_001749A0 0x001749A0u
#define F_00174A50 0x00174A50u
#define F_00175900 0x00175900u
#define F_00178B90 0x00178B90u
#define F_0017B490 0x0017B490u
#define F_001E8B90 0x001E8B90u
#define F_001EFD90 0x001EFD90u
#define F_001FB9F0 0x001FB9F0u

#define K_ONE 0x3F800000u
#define K_FIVE 0x40A00000u
#define K_TWELVE 0x41400000u
#define K_0_3 0x3E99999Au
#define K_M_0_2 0xBE4CCCCDu
#define K_DECAY 0x3C3A2E8Cu

typedef EmA01Math M;

static int done(const M *m) { return em_a01m_faulted(m) ? -1 : 0; }

/* The phase-2 / phase-3 move: 00178B90(p, 0). */
static void move(M *m, uint32_t p) { em_a01m_call_i(m, F_00178B90, 2, p, 0, 0, 0, NULL); }

/* ---- 00183250 -------------------------------------------------------------
 * Phase = byte +6 (the timer is the half +0x28, decremented and stored on
 * every read, the old value tested):
 * 0: +6 = 1, +7 = 0, +0x38 = 0.3, +0x25C = 2; clip = s16 of
 *    0017B490(p, 1, byte +0x235, byte +0x25C); 001749A0(p, clip, 0, 1.0);
 *    timer = 50.
 * 1: timer ran out -> +6 += 1, timer = 30.
 * 2: timer ran out -> +6 += 1, timer = 30; else the move.
 * 3: timer ran out -> +4 = 1, +5 = 0, +6 = 0, +0x1F0 = 0, SPR byte
 *    0x70003B8D = 0; else the move, then +0x38 -= 0.01137, and when that is
 *    below 0: +0x38 = 0 and 00174A50(p, 12.0).
 * Then (every phase, other phases included): +0xB4 += -0.2 and
 * 00175900(p, 1). */
int em_area01_math_00183250(M *m, uint32_t p)
{
    uint32_t phase = em_a01m_lbu(m, p + 6u);
    int32_t t;
    switch (phase) {
    case 0: {
        uint32_t clip = 0;
        em_a01m_sb(m, p + 6u, phase + 1u);
        em_a01m_sb(m, p + 7u, 0);
        em_a01m_sw(m, p + 0x38u, K_0_3);
        em_a01m_sb(m, p + 0x25Cu, 2);
        em_a01m_call_i(m, F_0017B490, 4, p, 1, em_a01m_lbu(m, p + 0x235u), em_a01m_lbu(m, p + 0x25Cu), &clip);
        clip = (uint32_t)(int32_t)(int16_t)(uint16_t)clip;
        em_a01m_call_if(m, F_001749A0, 3, p, clip, 0, 0, K_ONE, NULL, NULL);
        em_a01m_sh(m, p + 0x28u, 50);
        break;
    }
    case 1:
        t = em_a01m_lh(m, p + 0x28u);
        em_a01m_sh(m, p + 0x28u, (uint32_t)(t - 1));
        if (t == 0) {
            em_a01m_sb(m, p + 6u, em_a01m_lbu(m, p + 6u) + 1u);
            em_a01m_sh(m, p + 0x28u, 30);
        }
        break;
    case 2:
        t = em_a01m_lh(m, p + 0x28u);
        em_a01m_sh(m, p + 0x28u, (uint32_t)(t - 1));
        if (t == 0) {
            em_a01m_sb(m, p + 6u, em_a01m_lbu(m, p + 6u) + 1u);
            em_a01m_sh(m, p + 0x28u, 30);
        } else {
            move(m, p);
        }
        break;
    case 3:
        t = em_a01m_lh(m, p + 0x28u);
        em_a01m_sh(m, p + 0x28u, (uint32_t)(t - 1));
        if (t == 0) {
            em_a01m_sb(m, p + 4u, 1);
            em_a01m_sb(m, p + 5u, 0);
            em_a01m_sb(m, p + 6u, 0);
            em_a01m_sb(m, p + 0x1F0u, 0);
            em_a01m_sb(m, 0x70003B8Du, 0);
        } else {
            uint32_t f;
            move(m, p);
            f = em_ee_sub_bits(em_a01m_lw(m, p + 0x38u), K_DECAY);
            em_a01m_sw(m, p + 0x38u, f);
            if (em_ee_c_lt_bits(f, 0)) {
                em_a01m_sw(m, p + 0x38u, 0);
                em_a01m_call_if(m, F_00174A50, 1, p, 0, 0, 0, K_TWELVE, NULL, NULL);
            }
        }
        break;
    default:
        break;
    }
    em_a01m_sw(m, p + 0xB4u, em_ee_add_bits(em_a01m_lw(m, p + 0xB4u), K_M_0_2));
    em_a01m_call_i(m, F_00175900, 2, p, 1, 0, 0, NULL);
    return done(m);
}

/* ---- 00187DE0 ------------------------------------------------------------- */
int em_area01_math_00187DE0(M *m, uint32_t p)
{
    em_a01m_call_i(m, F_001031E0, 2, 0x700038B0u, 0x700031B0u, 0, 0, NULL);
    em_a01m_call_i(m, F_001EFD90, 3, 0x80000016u, 0x700038B0u, p + 0xC0u, 0, NULL);
    if (em_a01m_lbu(m, 0x00810700u) != 0x15)
        em_a01m_call_if(m, F_001E8B90, 1, 0x700038B0u, 0, 0, 0, K_FIVE, NULL, NULL);
    em_a01m_call_i(m, F_001FB9F0, 4, em_a01m_lbu(m, p + 0x23Cu) == 1 ? 0xCAu : 0xDBu, 0x1000, 0x1000, 0x1000,
                   NULL);
    return done(m);
}

/* ---- 00187EC0 ------------------------------------------------------------- */
int em_area01_math_00187EC0(M *m, uint32_t a0, uint32_t a1)
{
    em_a01m_sb(m, 0x008102BBu, 1);
    em_a01m_sb(m, 0x008104EAu, a0);
    em_a01m_sb(m, 0x008105CEu, a1);
    return done(m);
}
