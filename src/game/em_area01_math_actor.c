/* em_area01_math_actor.c - AREA01 lane "math": translations of the actor
 * helpers 001B13F0, 001B2140, 001C25E0, 001C2770, 001C39F0, 001C3BE0,
 * 001C3D60 and 001C69A0 (em_area01_math_actor.h, docs/AREA01_MATH.md).
 *
 * Read from the decomp's C (byte-matched: 001B13F0, 001C25E0, 001C39F0,
 * 001C3BE0, 001C3D60; NEARMISS: 001B2140, 001C2770, 001C69A0) and, for the
 * float operation order, the VU0 forms and the points where memory is
 * re-read after a call, from the original instructions. Every COP1 and VU0
 * macro operation goes through game/em_ee_float.h under its real form.
 * Verified by tools/test_area01_math_reference.py. */
#include "game/em_area01_math_actor.h"

#include "game/em_ee_float.h"

/* Callees (original addresses). */
#define COPY_QW4 0x00102958u
#define F_001026A0 0x001026A0u
#define F_001026D0 0x001026D0u
#define F_00102718 0x00102718u
#define F_001028B8 0x001028B8u
#define F_001028D0 0x001028D0u
#define F_001029C0 0x001029C0u
#define F_00102B08 0x00102B08u
#define F_00102BB0 0x00102BB0u
#define F_00102C58 0x00102C58u
#define F_001031E0 0x001031E0u
#define F_00103230 0x00103230u
#define F_0011E2A8 0x0011E2A8u
#define F_0011E620 0x0011E620u
#define F_0011E748 0x0011E748u
#define F_0019AB20 0x0019AB20u
#define F_0019B4C0 0x0019B4C0u
#define F_001B1470 0x001B1470u
#define F_001C2540 0x001C2540u
#define F_001C2690 0x001C2690u
#define F_001C3DB0 0x001C3DB0u
#define F_001C6160 0x001C6160u
#define F_001C9D50 0x001C9D50u
#define F_001FBD50 0x001FBD50u
#define QUAT_NLERP 0x001CA0A0u
#define QUAT_TO_MAT3 0x001CA1C0u

/* Float constants (binary32 bits). */
#define K_ONE 0x3F800000u
#define K_TWO 0x40000000u
#define K_THREE 0x40400000u
#define K_FOUR 0x40800000u
#define K_SIX 0x40C00000u
#define K_EIGHT 0x41000000u
#define K_M_ONE 0xBF800000u
#define K_M_TWO 0xC0000000u
#define K_M_THREE 0xC0400000u
#define K_M_FOUR 0xC0800000u
#define K_M_FIVE 0xC0A00000u
#define K_M_SIX 0xC0C00000u
#define K_M_0_7 0xBF333333u
#define K_M_0_8 0xBF4CCCCDu
#define K_0_04 0x3D23D70Au
#define K_0_06 0x3D75C28Fu
#define K_0_1 0x3DCCCCCDu
#define K_0_2 0x3E4CCCCDu
#define K_PI 0x40490FDBu
#define K_TWO_PI 0x40C90FDBu
#define K_56_25 0x42610000u
#define K_180 0x43340000u
#define K_300 0x43960000u
#define K_1_4096 0x39800000u

#define SPR(o) (0x70000000u + (o))

typedef EmA01Math M;

static int done(const M *m) { return em_a01m_faulted(m) ? -1 : 0; }

static void st4(M *m, uint32_t a, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    em_a01m_sw(m, a, x);
    em_a01m_sw(m, a + 4, y);
    em_a01m_sw(m, a + 8, z);
    em_a01m_sw(m, a + 12, w);
}

static void call2(M *m, uint32_t fn, uint32_t a0, uint32_t a1) { em_a01m_call_i(m, fn, 2, a0, a1, 0, 0, NULL); }
static void call3(M *m, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
    em_a01m_call_i(m, fn, 3, a0, a1, a2, 0, NULL);
}
static uint32_t call4r(M *m, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    uint32_t v0 = 0;
    em_a01m_call_i(m, fn, 4, a0, a1, a2, a3, &v0);
    return v0;
}
/* A callee taking one float (f12) and returning a float (f0). */
static uint32_t callf(M *m, uint32_t fn, uint32_t f12)
{
    uint32_t f0 = 0;
    em_a01m_call_if(m, fn, 0, 0, 0, 0, 0, f12, NULL, &f0);
    return f0;
}

/* ---- 001B13F0 -------------------------------------------------------------
 * 001028D0(0x70003600, a0, a1); r = 0011E748(x*x + y*y + z*z) over the
 * three result words (two products, then ACC = first + second and
 * ACC + z*z); v0 = (f12 < r) ? 0 : 1. */
int em_area01_math_001B13F0(M *m, uint32_t a0, uint32_t a1, uint32_t f12, uint32_t *v0)
{
    uint32_t x, y, z, acc, r = 0;
    call3(m, F_001028D0, SPR(0x3600), a0, a1);
    x = em_a01m_lw(m, SPR(0x3600));
    y = em_a01m_lw(m, SPR(0x3604));
    z = em_a01m_lw(m, SPR(0x3608));
    acc = em_ee_adda_bits(em_ee_mul_bits(x, x), em_ee_mul_bits(y, y));
    r = callf(m, F_0011E748, em_ee_madd_bits(acc, z, z));
    if (em_a01m_faulted(m)) return -1;
    if (v0) *v0 = em_ee_c_lt_bits(f12, r) ? 0u : 1u;
    return 0;
}

/* ---- 001B2140 -------------------------------------------------------------
 * 0 when D_008104E0 == 0x11. Otherwise a per-area test (D_00810700 area,
 * D_00810701 sub-state, D_00810702 the third area byte) against the node's
 * byte +0x9E (and, for area 3, +0x9D); 1 = pass. Areas without a case
 * (and areas >= 23) give 0. */
static uint32_t room_is(uint32_t room, const uint8_t *set, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (room == set[i]) return 1;
    return 0;
}

/* "The room is the area byte" for the listed values, "the room is in the
 * group" for a grouped value, and "the room is none of them" otherwise. */
static uint32_t b2140_area(M *m, uint32_t node, uint32_t area)
{
    uint32_t sub = em_a01m_lbu(m, 0x00810701u);
    uint32_t third = em_a01m_lbu(m, 0x00810702u);
    uint32_t room;
#define ROOM() (room = em_a01m_lbu(m, node + 0x9Eu))
#define IN(...) room_is(room, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))
    switch (area) {
    case 0:
        switch (third) {
        case 5: case 6: ROOM(); return IN(5, 6);
        case 8: case 11: return em_a01m_lbu(m, node + 0x9Eu) == third;
        default: ROOM(); return !IN(11, 8, 6, 5);
        }
    case 1:
        switch (third) {
        case 1: case 8: return em_a01m_lbu(m, node + 0x9Eu) == third;
        default: ROOM(); return !IN(8, 1);
        }
    case 2:
        return sub == 1 ? 0 : 1;
    case 3:
        if (sub != em_a01m_lbu(m, node + 0x9Du)) return 0;
        if (sub == 0) {
            switch (third) {
            case 0: case 2: ROOM(); return IN(0, 2);
            default: ROOM(); return !IN(0, 2);
            }
        }
        switch (third) {
        case 2: case 4: case 7: return em_a01m_lbu(m, node + 0x9Eu) == third;
        default: ROOM(); return !IN(2, 4, 7);
        }
    case 4:
        switch (third) {
        case 4: case 5: ROOM(); return IN(4, 5);
        case 9: case 10: ROOM(); return IN(9, 10);
        default: ROOM(); return !IN(10, 9, 5, 4);
        }
    case 6:
        if (third == 1) return em_a01m_lbu(m, node + 0x9Eu) == 1;
        return em_a01m_lbu(m, node + 0x9Eu) == 1 ? 0 : 1;
    case 7:
        switch (sub) {
        case 0:
            switch (third) {
            case 1: case 8: case 10: case 11: return em_a01m_lbu(m, node + 0x9Eu) == third;
            case 3: case 7: case 9: case 12: ROOM(); return IN(3, 7, 9, 12);
            default: ROOM(); return !IN(12, 9, 7, 3, 11, 10, 8, 1);
            }
        case 2:
            return 1;
        case 3:
            if (third == 2) return em_a01m_lbu(m, node + 0x9Eu) == 2;
            return em_a01m_lbu(m, node + 0x9Eu) == 2 ? 0 : 1;
        }
        return 0;
    case 8:
        switch (sub) {
        case 0:
            switch (third) {
            case 1: case 6: return em_a01m_lbu(m, node + 0x9Eu) == third;
            case 3: case 7: ROOM(); return IN(3, 7);
            default: ROOM(); return !IN(7, 6, 3, 1);
            }
        case 2:
            return 1;
        case 3:
            if (third == 2) return em_a01m_lbu(m, node + 0x9Eu) == 2;
            return em_a01m_lbu(m, node + 0x9Eu) == 2 ? 0 : 1;
        }
        return 0;
    case 11: case 14: case 15: case 17: case 20: case 21:
        return 1;
    case 13:
        switch (third) {
        case 0: case 1: ROOM(); return IN(0, 1);
        case 2: case 3: ROOM(); return IN(2, 3);
        case 4: case 6: ROOM(); return IN(4, 6);
        case 5: case 7: ROOM(); return IN(5, 7);
        default: ROOM(); return room >= 8;
        }
    case 16:
        switch (sub) {
        case 0:
            if (third == 2) return em_a01m_lbu(m, node + 0x9Eu) == 2;
            return em_a01m_lbu(m, node + 0x9Eu) == 2 ? 0 : 1;
        case 1:
            switch (third) {
            case 5: return em_a01m_lbu(m, node + 0x9Eu) == 5;
            case 2: case 4: case 6: ROOM(); return IN(2, 4, 6);
            default: ROOM(); return !IN(6, 5, 4, 2);
            }
        }
        return 0;
    case 18:
        if (third == 1) return em_a01m_lbu(m, node + 0x9Eu) == 1;
        return em_a01m_lbu(m, node + 0x9Eu) == 1 ? 0 : 1;
    case 19:
        if (sub == 0) {
            switch (third) {
            case 4: return em_a01m_lbu(m, node + 0x9Eu) == 4;
            case 5: case 7: case 8: case 9: ROOM(); return IN(5, 7, 8, 9);
            default: ROOM(); return !IN(9, 8, 7, 5, 4);
            }
        }
        switch (third) {
        case 0: case 7: ROOM(); return IN(0, 7);
        case 1: case 3: case 6: ROOM(); return IN(1, 3, 6);
        default: ROOM(); return !IN(7, 6, 3, 1, 0);
        }
    case 22:
        switch (third) {
        case 4: return em_a01m_lbu(m, node + 0x9Eu) == 4;
        case 0: case 2: ROOM(); return IN(0, 2);
        default: ROOM(); return !IN(4, 2, 0);
        }
    }
    return 0;
#undef IN
#undef ROOM
}

int em_area01_math_001B2140(M *m, uint32_t node, uint32_t *v0)
{
    uint32_t r = 0;
    if (em_a01m_lw(m, 0x008104E0u) != 0x11u) r = b2140_area(m, node, em_a01m_lbu(m, 0x00810700u));
    if (em_a01m_faulted(m)) return -1;
    if (v0) *v0 = r;
    return 0;
}

/* ---- 001C25E0 -------------------------------------------------------------
 * SPR 0x700038B0 = (0, -4.0, 0, 1.0); 001026A0(0x700038C0, node + 0xD0, a1);
 * 001028B8(0x700038C0, 0x700038C0, node + 0xB0);
 * 001026A0(0x700038D0, node + 0xD0, 0x700038B0);
 * 0019B4C0(node, 0x700038C0, 0x700038D0, 6). */
int em_area01_math_001C25E0(M *m, uint32_t node, uint32_t a1)
{
    st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
    call3(m, F_001026A0, SPR(0x38C0), node + 0xD0u, a1);
    call3(m, F_001028B8, SPR(0x38C0), SPR(0x38C0), node + 0xB0u);
    call3(m, F_001026A0, SPR(0x38D0), node + 0xD0u, SPR(0x38B0));
    call4r(m, F_0019B4C0, node, SPR(0x38C0), SPR(0x38D0), 6);
    return done(m);
}

/* ---- 001C39F0 -------------------------------------------------------------
 * Nothing when the distance f12 is 0. mode = state[+0xE4] >> 8 (arithmetic).
 * SPR 0x70003600 = (0 or the side term, 0, f12, 1.0); 001026A0(0x70003610,
 * node + 0xD0, 0x70003600); node +0xB0..B8 += 0x70003610..18.
 * Modes other than 0 and 4 also: side term = (f12 / 2) * 0011E2A8(+0xDC),
 * n = (float)001C6160(node), state[+0xDC] = 001B1470(2pi * ((n - node[+0x3C])
 * / n)) (stored before the step), and after the step node +0xB0..B8 -=
 * state[+0x80..0x88] / 2. */
int em_area01_math_001C39F0(M *m, uint32_t node, uint32_t state, uint32_t f12)
{
    uint32_t mode, a, b, c, f0 = 0, n, v0 = 0;
    if (em_ee_c_eq_bits(0, f12)) return done(m);
    mode = (uint32_t)((int32_t)em_a01m_lw(m, state + 0xE4u) >> 8);
    if (mode == 0 || mode == 4) {
        em_a01m_sw(m, SPR(0x360C), K_ONE);
        em_a01m_sw(m, SPR(0x3604), 0);
        em_a01m_sw(m, SPR(0x3608), f12);
        em_a01m_sw(m, SPR(0x3600), 0);
        call3(m, F_001026A0, SPR(0x3610), node + 0xD0u, SPR(0x3600));
    } else {
        em_a01m_sw(m, SPR(0x360C), K_ONE);
        em_a01m_sw(m, SPR(0x3604), 0);
        em_a01m_sw(m, SPR(0x3608), f12);
        f0 = callf(m, F_0011E2A8, em_a01m_lw(m, state + 0xDCu));
        em_a01m_sw(m, SPR(0x3600), em_ee_mul_bits(em_ee_div_bits(f12, K_TWO), f0));
        em_a01m_call_i(m, F_001C6160, 1, node, 0, 0, 0, &v0);
        n = em_ee_cvt_s_w_bits(v0);
        a = em_ee_div_bits(em_ee_sub_bits(n, em_a01m_lw(m, node + 0x3Cu)), n);
        em_a01m_sw(m, state + 0xDCu, callf(m, F_001B1470, em_ee_mul_bits(K_TWO_PI, a)));
        call3(m, F_001026A0, SPR(0x3610), node + 0xD0u, SPR(0x3600));
    }
    a = em_ee_add_bits(em_a01m_lw(m, node + 0xB0u), em_a01m_lw(m, SPR(0x3610)));
    em_a01m_sw(m, node + 0xB0u, a);
    b = em_ee_add_bits(em_a01m_lw(m, node + 0xB4u), em_a01m_lw(m, SPR(0x3614)));
    em_a01m_sw(m, node + 0xB4u, b);
    c = em_ee_add_bits(em_a01m_lw(m, node + 0xB8u), em_a01m_lw(m, SPR(0x3618)));
    em_a01m_sw(m, node + 0xB8u, c);
    if (mode != 0 && mode != 4) {
        for (uint32_t k = 0; k < 12; k += 4) {
            uint32_t half = em_ee_div_bits(em_a01m_lw(m, state + 0x80u + k), K_TWO);
            em_a01m_sw(m, node + 0xB0u + k, em_ee_sub_bits(em_a01m_lw(m, node + 0xB0u + k), half));
        }
    }
    return done(m);
}

/* ---- 001C3BE0 -------------------------------------------------------------
 * 001029C0(0x70003000); rows 0x70003020 = state +0x70..78, 0x70003010 =
 * state +0x80..88; 0x70003610 = (+0x80..88, 1.0), 0x70003620 = (+0x70..78,
 * 1.0); 00102718(0x70003600, 0x70003610, 0x70003620); 0x70003000..08 =
 * 0x70003600..08; 001029C0(0x70003440); 00102B08(0x70003440, 0x70003440,
 * node +0xC0); 00102BB0(0x70003440, 0x70003440, node +0xC4);
 * 001026D0(0x70003000, 0x70003000, 0x70003440). */
int em_area01_math_001C3BE0(M *m, uint32_t node, uint32_t state)
{
    em_a01m_call_i(m, F_001029C0, 1, SPR(0x3000), 0, 0, 0, NULL);
    for (uint32_t k = 0; k < 12; k += 4) em_a01m_sw(m, SPR(0x3020) + k, em_a01m_lw(m, state + 0x70u + k));
    for (uint32_t k = 0; k < 12; k += 4) em_a01m_sw(m, SPR(0x3010) + k, em_a01m_lw(m, state + 0x80u + k));
    for (uint32_t k = 0; k < 12; k += 4) em_a01m_sw(m, SPR(0x3610) + k, em_a01m_lw(m, state + 0x80u + k));
    em_a01m_sw(m, SPR(0x361C), K_ONE);
    for (uint32_t k = 0; k < 12; k += 4) em_a01m_sw(m, SPR(0x3620) + k, em_a01m_lw(m, state + 0x70u + k));
    em_a01m_sw(m, SPR(0x362C), K_ONE);
    call3(m, F_00102718, SPR(0x3600), SPR(0x3610), SPR(0x3620));
    for (uint32_t k = 0; k < 12; k += 4) em_a01m_sw(m, SPR(0x3000) + k, em_a01m_lw(m, SPR(0x3600) + k));
    em_a01m_call_i(m, F_001029C0, 1, SPR(0x3440), 0, 0, 0, NULL);
    em_a01m_call_if(m, F_00102B08, 2, SPR(0x3440), SPR(0x3440), 0, 0, em_a01m_lw(m, node + 0xC0u), NULL, NULL);
    em_a01m_call_if(m, F_00102BB0, 2, SPR(0x3440), SPR(0x3440), 0, 0, em_a01m_lw(m, node + 0xC4u), NULL, NULL);
    call3(m, F_001026D0, SPR(0x3000), SPR(0x3000), SPR(0x3440));
    return done(m);
}

/* ---- 001C3D60 -------------------------------------------------------------
 * 001C3BE0(node, a1 as received); copy_qw4(node + 0xD0, 0x70003000);
 * 001031E0(0x70003030, node + 0xB0). */
int em_area01_math_001C3D60(M *m, uint32_t node, uint32_t state)
{
    em_area01_math_001C3BE0(m, node, state);
    call2(m, COPY_QW4, node + 0xD0u, SPR(0x3000));
    call2(m, F_001031E0, SPR(0x3030), node + 0xB0u);
    return done(m);
}

/* ---- 001C2770 -------------------------------------------------------------
 * The per-frame state machine. `st` is the state block: +0xE4 packs the
 * phase (low 4 bits) and the probe sub-state (>> 8, arithmetic); +0xD8 a
 * distance handed to 001C39F0; +0xF0 a vertical speed; +0xD4 the blend
 * parameter. `flags` bits 0, 1, 2 select the variants below. The hit record
 * is the word at SPR 0x700031D0 (re-read where the original re-reads it):
 * +0x1A its signed flag halfword, +0x24..2C a vector. v0 = 1 when the frame
 * latched or blends, 8 for the early "not grabbable" returns, else 0. */
static uint32_t hitp(M *m) { return em_a01m_lw(m, SPR(0x31D0)); }
static int32_t surf(M *m) { return em_a01m_lh(m, hitp(m) + 0x1Au); }
static void e4_and(M *m, uint32_t st, uint32_t v) { em_a01m_sw(m, st + 0xE4u, em_a01m_lw(m, st + 0xE4u) & v); }
static void e4_or(M *m, uint32_t st, uint32_t v) { em_a01m_sw(m, st + 0xE4u, em_a01m_lw(m, st + 0xE4u) | v); }
static uint32_t probe(M *m, uint32_t act, uint32_t from)
{
    return call4r(m, F_001C2540, act, from, SPR(0x38B0), act + 0xD0u);
}
static uint32_t probe2(M *m, uint32_t act, uint32_t from)
{
    uint32_t v0 = 0;
    em_a01m_call_i(m, F_001C2690, 3, act, from, SPR(0x38B0), 0, &v0);
    return v0;
}
static void sound_1ac(M *m, uint32_t act)
{
    em_a01m_call_if(m, F_001FBD50, 3, act, 0x1AC, 0, 0, K_300, NULL, NULL);
}
static void slide_push(M *m, uint32_t act, uint32_t hit)
{
    em_a01m_sw(m, SPR(0x3610), em_a01m_lw(m, hit + 0x24u));
    em_a01m_sw(m, SPR(0x3614), em_a01m_lw(m, hit + 0x28u));
    em_a01m_sw(m, SPR(0x3618), em_a01m_lw(m, hit + 0x2Cu));
    em_a01m_sw(m, SPR(0x361C), K_ONE);
    em_a01m_call_if(m, F_00103230, 3, SPR(0x3610), SPR(0x3610), hit, 0, K_0_2, NULL, NULL);
    call3(m, F_001028B8, act + 0xB0u, SPR(0x3610), act + 0xB0u);
}
static uint32_t yaw_step(M *m, uint32_t act)
{
    uint32_t f0 = 0;
    const uint32_t f[2] = {em_a01m_lw(m, SPR(0x3620)), em_a01m_lw(m, SPR(0x3628))};
    em_a01m_call(m, F_0011E620, NULL, 0, f, 2, NULL, &f0);
    return callf(m, F_001B1470, em_ee_add_bits(em_a01m_lw(m, act + 0xC4u), f0));
}

int em_area01_math_001C2770(M *m, uint32_t act, uint32_t st, uint32_t flags, uint32_t *v0)
{
    uint32_t state = em_a01m_lw(m, st + 0xE4u);
    uint32_t result = 0, phase = state & 0xFu;
    int32_t sub = (int32_t)state >> 8;
    uint32_t latch = 0, hit, f;
    int32_t s;

    switch (phase) {
    case 0:
        latch = 0;
        em_area01_math_001C39F0(m, act, st, em_a01m_lw(m, st + 0xD8u));
        st4(m, SPR(0x38A0), 0, K_TWO, K_FOUR, K_ONE);
        switch (sub) {
        case 0:
            st4(m, SPR(0x38B0), 0, 0, K_SIX, K_ONE);
            if (probe(m, act, SPR(0x38A0)) != 0) {
                s = surf(m);
                if (s & 0x8000) {
                    latch = 1; result = 1;
                    e4_and(m, st, 0xF0);
                    em_a01m_sw(m, act + 0xC0u, 0);
                    e4_or(m, st, 3);
                    em_a01m_sw(m, st + 0xD8u, 0);
                } else if (s & 0x2800) {
                    latch = 1; result = 1;
                    e4_and(m, st, 0xF0);
                    em_a01m_sw(m, act + 0xC0u, 0);
                    e4_or(m, st, 2);
                    em_a01m_sw(m, st + 0xD8u, 0);
                }
            } else {
                st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
                probe(m, act, st + 0x60u);
            }
            break;
        case 1: case 2: case 3:
            if (em_ee_c_eq_bits(0, em_a01m_lw(m, st + 0xD8u))) break;
            st4(m, SPR(0x38B0), 0, 0, K_SIX, K_ONE);
            if (probe(m, act, SPR(0x38A0)) != 0) {
                if (flags & 1) {
                    int keep = 0;
                    switch (sub) {
                    case 1: keep = (surf(m) & 0x5000) != 0; break;
                    case 2: keep = (surf(m) & 0x2800) != 0; break;
                    case 3: keep = (surf(m) & 0x8000) != 0; break;
                    default: break;
                    }
                    if (!keep) {
                        st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
                        probe(m, act, st + 0x60u);
                        if (em_a01m_faulted(m)) return -1;
                        if (v0) *v0 = 8;
                        return 0;
                    }
                }
                latch = 1;
                e4_and(m, st, 0xF0);
                s = surf(m);
                result = 1;
                e4_or(m, st, (s & 0x8000) ? 3u : (s & 0x2800) ? 2u : 1u);
            } else {
                st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
                if (probe(m, act, st + 0x60u) != 0) {
                    latch = 2;
                } else {
                    st4(m, SPR(0x38C0), 0, K_M_TWO, 0, K_ONE);
                    call3(m, F_001026A0, SPR(0x38C0), act + 0xD0u, SPR(0x38C0));
                    call3(m, F_001028B8, SPR(0x38C0), SPR(0x38C0), act + 0xB0u);
                    st4(m, SPR(0x38D0), 0, K_M_FIVE, 0, 0);
                    if (call4r(m, F_0019AB20, act, SPR(0x38C0), SPR(0x38D0), 7) == 0) {
                        em_a01m_sw(m, SPR(0x38A0), 0);
                        em_a01m_sw(m, SPR(0x38B0), 0);
                        em_a01m_sw(m, SPR(0x38B4), 0);
                        em_a01m_sw(m, SPR(0x38A4), K_M_THREE);
                        em_a01m_sw(m, SPR(0x38A8), K_M_ONE);
                        em_a01m_sw(m, SPR(0x38AC), K_ONE);
                        em_a01m_sw(m, SPR(0x38B8), K_M_SIX);
                        em_a01m_sw(m, SPR(0x38BC), K_ONE);
                        if (probe(m, act, SPR(0x38A0)) != 0) {
                            latch = 3;
                            e4_and(m, st, 0xF0);
                            s = surf(m);
                            result = 1;
                            e4_or(m, st, (s & 0x8000) ? 3u : (s & 0x2800) ? 2u : 1u);
                        } else {
                            latch = 0x11; result = 1;
                            e4_and(m, st, 0xF0);
                            e4_or(m, st, 4);
                            em_a01m_sw(m, st + 0xF0u, 0);
                        }
                    }
                }
            }
            break;
        case 4: case 5: case 6: {
            uint32_t sp;
            if (sub == 4) {
                uint32_t t = em_ee_neg_bits(em_a01m_lw(m, st + 0xF0u));
                t = em_ee_mul_bits(K_PI, em_ee_mul_bits(K_56_25, t));
                em_a01m_sw(m, act + 0xC0u, em_ee_div_bits(t, K_180));
            }
            sp = em_ee_sub_bits(em_a01m_lw(m, st + 0xF0u), sub == 6 ? K_0_06 : K_0_04);
            em_a01m_sw(m, st + 0xF0u, sp);
            if (em_ee_c_lt_bits(em_a01m_lw(m, st + 0xF0u), K_M_0_8)) em_a01m_sw(m, st + 0xF0u, K_M_0_8);
            em_a01m_sw(m, act + 0xB4u, em_ee_add_bits(em_a01m_lw(m, act + 0xB4u), em_a01m_lw(m, st + 0xF0u)));
            if (sub == 4) {
                st4(m, SPR(0x38B0), 0, 0, K_EIGHT, K_ONE);
                if (probe(m, act, SPR(0x38A0)) != 0) {
                    hit = hitp(m);
                    s = em_a01m_lh(m, hit + 0x1Au);
                    if (s & 0x8000) {
                        if (!(flags & 4)) {
                            em_a01m_sw(m, st + 0xD8u, 0);
                            latch = 1; result = 1;
                            e4_and(m, st, 0xF0);
                            em_a01m_sw(m, act + 0xC0u, 0);
                            e4_or(m, st, 3);
                        }
                    } else if (s & 0x2800) {
                        if (flags & 4) {
                            slide_push(m, act, hit);
                        } else {
                            em_a01m_sw(m, st + 0xD8u, 0);
                            latch = 1; result = 1;
                            e4_and(m, st, 0xF0);
                            em_a01m_sw(m, act + 0xC0u, 0);
                            e4_or(m, st, 2);
                        }
                    } else {
                        em_a01m_sw(m, st + 0xD8u, 0);
                        e4_and(m, st, 0xF0);
                        if (flags & 2) {
                            latch = 9; result = 1;
                            e4_or(m, st, 1);
                            sound_1ac(m, act);
                        } else {
                            em_a01m_sw(m, act + 0xC0u, 0);
                            latch = 1;
                            e4_or(m, st, 1);
                            result = 1;
                            sound_1ac(m, act);
                        }
                    }
                } else {
                    st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
                    if (probe(m, act, st + 0x60u) != 0) {
                        if (surf(m) & 0x2800) {
                            if (!(flags & 4)) {
                                em_a01m_sw(m, st + 0xD8u, 0);
                                latch = 5; result = 1;
                                e4_and(m, st, 0xF0);
                                e4_or(m, st, 2);
                                em_a01m_sw(m, act + 0xC0u, 0);
                            }
                        } else {
                            em_a01m_sw(m, st + 0xD8u, 0);
                            latch = 5;
                            e4_and(m, st, 0xF0);
                            result = 1;
                            e4_or(m, st, 1);
                            em_a01m_sw(m, act + 0xC0u, 0);
                            sound_1ac(m, act);
                        }
                    }
                }
            } else {
                /* sub 5 probes with 001C2540, sub 6 with 001C2690. */
                int five = sub == 5;
                int landed = 0;
                st4(m, SPR(0x38B0), 0, 0, K_EIGHT, K_ONE);
                if ((five ? probe(m, act, SPR(0x38A0)) : probe2(m, act, SPR(0x38A0))) != 0 &&
                    em_ee_c_lt_bits(em_a01m_lw(m, st + 0xF0u), 0)) {
                    hit = hitp(m);
                    s = em_a01m_lh(m, hit + 0x1Au);
                    if (s & 0x5000) {
                        em_a01m_sw(m, st + 0xD8u, 0);
                        e4_and(m, st, 0xF0);
                        if (flags & 2) {
                            latch = 9; result = 1;
                            e4_or(m, st, 1);
                            sound_1ac(m, act);
                        } else {
                            em_a01m_sw(m, act + 0xC0u, 0);
                            latch = 1;
                            e4_or(m, st, 1);
                            result = 1;
                            sound_1ac(m, act);
                        }
                        landed = 1;
                    } else if (s & 0x2800) {
                        slide_push(m, act, hit);
                    }
                }
                if (!landed) {
                    st4(m, SPR(0x38B0), 0, K_M_FOUR, 0, K_ONE);
                    if ((five ? probe(m, act, st + 0x60u) : probe2(m, act, st + 0x60u)) != 0 &&
                        em_ee_c_lt_bits(em_a01m_lw(m, st + 0xF0u), 0) && (surf(m) & 0x5000)) {
                        if (five) {
                            em_a01m_sw(m, st + 0xD8u, 0);
                            em_a01m_sw(m, act + 0xC0u, 0);
                            latch = 5;
                            e4_and(m, st, 0xF0);
                            result = 1;
                            e4_or(m, st, 1);
                            sound_1ac(m, act);
                        } else {
                            e4_and(m, st, 0xF0);
                            em_a01m_sw(m, st + 0xD8u, 0);
                            if (flags & 2) {
                                latch = 9; result = 1;
                                e4_or(m, st, 1);
                                sound_1ac(m, act);
                            } else {
                                em_a01m_sw(m, act + 0xC0u, 0);
                                latch = 5; result = 1;
                                e4_or(m, st, 1);
                                sound_1ac(m, act);
                            }
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
        }

        /* Commit the latched surface. */
        if (latch != 0) {
            if (latch & 0x10) {
                st4(m, SPR(0x3610), 0, K_ONE, 0, K_ONE);
                em_a01m_call_i(m, F_001C3DB0, 4, st + 0x80u, SPR(0x3610), st + 0x70u, SPR(0x3620), NULL);
                em_a01m_sw(m, act + 0xC4u, yaw_step(m, act));
                st4(m, st + 0x70u, 0, 0, K_ONE, K_ONE);
                em_a01m_sw(m, st + 0x80u, 0);
                em_a01m_sw(m, st + 0x84u, K_ONE);
                latch = 5;
                em_a01m_sw(m, st + 0x88u, 0);
            } else {
                hit = hitp(m);
                em_a01m_sw(m, SPR(0x3610), em_a01m_lw(m, hit + 0x24u));
                em_a01m_sw(m, SPR(0x3614), em_a01m_lw(m, hit + 0x28u));
                em_a01m_sw(m, SPR(0x3618), em_a01m_lw(m, hit + 0x2Cu));
                em_a01m_sw(m, SPR(0x361C), K_ONE);
                em_a01m_call_i(m, F_001C3DB0, 4, st + 0x80u, SPR(0x3610), st + 0x70u, SPR(0x3620), NULL);
                if (em_ee_c_eq_bits(em_a01m_lw(m, hitp(m) + 0x28u), K_ONE)) {
                    em_a01m_sw(m, act + 0xC4u, yaw_step(m, act));
                    st4(m, st + 0x70u, 0, 0, K_ONE, K_ONE);
                } else {
                    call2(m, F_001031E0, st + 0x70u, SPR(0x3620));
                }
                em_a01m_sw(m, st + 0x80u, em_a01m_lw(m, hitp(m) + 0x24u));
                em_a01m_sw(m, st + 0x84u, em_a01m_lw(m, hitp(m) + 0x28u));
                em_a01m_sw(m, st + 0x88u, em_a01m_lw(m, hitp(m) + 0x2Cu));
            }
        }
        if (latch & 1) {
            em_area01_math_001C3BE0(m, act, st);
            if (latch & 8) st4(m, SPR(0x38A0), 0, K_TWO, K_FOUR, K_ONE);
            else if (latch & 4) st4(m, SPR(0x38A0), 0, 0, 0, K_ONE);
            else if (latch & 2) st4(m, SPR(0x38A0), 0, K_M_FOUR, K_M_0_7, K_ONE);
            else st4(m, SPR(0x38A0), 0, K_THREE, K_FOUR, K_ONE);
            call3(m, F_001026A0, SPR(0x38A0), act + 0xD0u, SPR(0x38A0));
            if (latch & 8) {
                for (uint32_t k = 0; k < 12; k += 4)
                    em_a01m_sw(m, act + 0xB0u + k,
                               em_ee_add_bits(em_a01m_lw(m, act + 0xB0u + k), em_a01m_lw(m, SPR(0x38A0) + k)));
                call2(m, COPY_QW4, act + 0xD0u, SPR(0x3000));
                em_a01m_sw(m, st + 0xE4u, 0x100);
                result = 0;
            } else {
                for (uint32_t k = 0; k < 12; k += 4)
                    em_a01m_sw(m, SPR(0x38A0) + k,
                               em_ee_add_bits(em_a01m_lw(m, SPR(0x38A0) + k), em_a01m_lw(m, act + 0xB0u + k)));
                em_a01m_sw(m, SPR(0x38AC), K_ONE);
                call2(m, COPY_QW4, st + 0x90u, SPR(0x3000));
                call2(m, F_001031E0, st + 0xC0u, SPR(0x38A0));
                call2(m, F_001031E0, act + 0x100u, act + 0xB0u);
                call2(m, COPY_QW4, SPR(0x3000), act + 0xD0u);
                em_a01m_sw(m, st + 0xD4u, 0);
            }
        }
        break;

    case 1: case 2: case 3: case 4: case 5: case 6:
        result = 1;
        em_a01m_call_if(m, F_001C9D50, 3, SPR(0x3000), act + 0xD0u, st + 0x90u, 0, em_a01m_lw(m, st + 0xD4u),
                        NULL, NULL);
        f = em_ee_add_bits(em_a01m_lw(m, st + 0xD4u), K_0_1);
        em_a01m_sw(m, st + 0xD4u, f);
        if (!em_ee_c_le_bits(f, K_ONE)) {
            call2(m, COPY_QW4, SPR(0x3000), st + 0x90u);
            call2(m, COPY_QW4, act + 0xD0u, st + 0x90u);
            em_a01m_sw(m, act + 0xB0u, em_a01m_lw(m, act + 0x100u));
            em_a01m_sw(m, act + 0xB4u, em_a01m_lw(m, act + 0x104u));
            em_a01m_sw(m, act + 0xB8u, em_a01m_lw(m, act + 0x108u));
            em_a01m_sw(m, act + 0x100u, 0);
            em_a01m_sw(m, act + 0x104u, 0);
            em_a01m_sw(m, act + 0x108u, 0);
            em_a01m_sw(m, st + 0xE4u, phase << 8);
        }
        break;
    default:
        break;
    }
    if (em_a01m_faulted(m)) return -1;
    if (v0) *v0 = result;
    return 0;
}

/* ---- 001C69A0 -------------------------------------------------------------
 * SPR 0x70003400 rows 0..2 (xyz) scaled by model +0x60 x / y / z (VMULbc,
 * dest xyz), row 3 stored back. Then for each of the model's
 * byte(+0x0C) bones (pointer table at +0x110):
 *   quat_nlerp(0x70003600, bone + 0x30, bone + 0x40, bone +0x50);
 *   quat_to_mat3(0x70003440, 0x70003600, bone); rows 0x3440/50/60 (xyz)
 *   scaled by bone +0x18 / +0x1C / +0x20;
 *   001029C0(0x70003480); 00102C58(0x70003480, 0x70003480, bone + 0x70);
 *   0x700034B0..B8 = bone +0x7C..84; rows 0x3480/90/A0 (xyz) scaled by
 *   (1/4096) * (float)(s16 at bone +0x88 / +0x8A / +0x8C);
 *   0x3480 = the product of the rows of 0x3480 with the 0x3440 matrix
 *   (ACC chain x, y, z, then + w); bone +0x90 = the product of 0x3480 with
 *   the parent's +0x90 matrix (s16 bone +0x64 != -1) or with 0x3400. */
static void lq(M *m, uint32_t a, uint32_t v[4])
{
    a &= ~15u;
    for (int k = 0; k < 4; k++) v[k] = em_a01m_lw(m, a + 4u * (uint32_t)k);
}
static void sq(M *m, uint32_t a, const uint32_t v[4])
{
    a &= ~15u;
    for (int k = 0; k < 4; k++) em_a01m_sw(m, a + 4u * (uint32_t)k, v[k]);
}
static int vu(M *m, em_vu_op op, unsigned dest, int bc, const uint32_t fs[4], const uint32_t ft[4],
              const uint32_t acc[4], uint32_t dst[4])
{
    if (em_vu_vec_bits(op, dest, bc, fs, ft, 0, acc, dst) != EM_EE_FLOAT_OK) {
        if (!em_a01m_faulted(m)) { m->fault_code = EM_A01M_FAULT_ADDRESS; m->fault_address = 0x001C69A0u; }
        return -1;
    }
    return 0;
}
/* One row pass: out row j = ACC(a0 * b_j.x + a1 * b_j.y + a2 * b_j.z) +
 * a3 * b_j.w, each row stored before the next row of `b` is loaded. */
static void mat_rows(M *m, uint32_t a, uint32_t b, uint32_t out)
{
    uint32_t r[4][4], bj[4], acc[4] = {0, 0, 0, 0}, o[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < 4; i++) lq(m, a + 16u * i, r[i]);
    for (uint32_t j = 0; j < 4; j++) {
        lq(m, b + 16u * j, bj);
        vu(m, EM_VU_MULABC, 15, 0, r[0], bj, NULL, acc);
        vu(m, EM_VU_MADDABC, 15, 1, r[1], bj, acc, acc);
        vu(m, EM_VU_MADDABC, 15, 2, r[2], bj, acc, acc);
        vu(m, EM_VU_MADDBC, 15, 3, r[3], bj, acc, o);
        sq(m, out + 16u * j, o);
    }
}
static void scale_row(M *m, uint32_t row, uint32_t s)
{
    uint32_t v[4], t[4] = {s, 0, 0, 0};
    lq(m, row, v);
    vu(m, EM_VU_MULBC, 14, 0, v, t, NULL, v);
    sq(m, row, v);
}

int em_area01_math_001C69A0_root(M *m, uint32_t model)
{
    uint32_t r[4][4], sc[4];
    for (uint32_t i = 0; i < 4; i++) lq(m, SPR(0x3400) + 16u * i, r[i]);
    lq(m, model + 0x60u, sc);
    vu(m, EM_VU_MULBC, 14, 0, r[0], sc, NULL, r[0]);
    vu(m, EM_VU_MULBC, 14, 1, r[1], sc, NULL, r[1]);
    vu(m, EM_VU_MULBC, 14, 2, r[2], sc, NULL, r[2]);
    for (uint32_t i = 0; i < 4; i++) sq(m, SPR(0x3400) + 16u * i, r[i]);
    return done(m);
}

int em_area01_math_001C69A0_bone(M *m, uint32_t model, uint32_t bone)
{
    int32_t parent;
    call3(m, QUAT_TO_MAT3, SPR(0x3440), SPR(0x3600), bone);
    scale_row(m, SPR(0x3440), em_a01m_lw(m, bone + 0x18u));
    scale_row(m, SPR(0x3450), em_a01m_lw(m, bone + 0x1Cu));
    scale_row(m, SPR(0x3460), em_a01m_lw(m, bone + 0x20u));
    em_a01m_call_i(m, F_001029C0, 1, SPR(0x3480), 0, 0, 0, NULL);
    call3(m, F_00102C58, SPR(0x3480), SPR(0x3480), bone + 0x70u);
    em_a01m_sw(m, SPR(0x34B0), em_a01m_lw(m, bone + 0x7Cu));
    em_a01m_sw(m, SPR(0x34B4), em_a01m_lw(m, bone + 0x80u));
    em_a01m_sw(m, SPR(0x34B8), em_a01m_lw(m, bone + 0x84u));
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t w = em_ee_cvt_s_w_bits((uint32_t)em_a01m_lh(m, bone + 0x88u + 2u * k));
        scale_row(m, SPR(0x3480) + 16u * k, em_ee_mul_bits(K_1_4096, w));
    }
    mat_rows(m, SPR(0x3440), SPR(0x3480), SPR(0x3480));
    parent = em_a01m_lh(m, bone + 0x64u);
    if (parent != -1)
        mat_rows(m, em_a01m_lw(m, model + 0x110u + 4u * (uint32_t)parent) + 0x90u, SPR(0x3480), bone + 0x90u);
    else
        mat_rows(m, SPR(0x3400), SPR(0x3480), bone + 0x90u);
    return done(m);
}

int em_area01_math_001C69A0(M *m, uint32_t model)
{
    if (em_area01_math_001C69A0_root(m, model) < 0) return -1;
    for (uint32_t i = 0; (int32_t)i < (int32_t)em_a01m_lbu(m, model + 0x0Cu); i++) {
        uint32_t bone = em_a01m_lw(m, model + 0x110u + 4u * i);
        em_a01m_call_if(m, QUAT_NLERP, 3, SPR(0x3600), bone + 0x30u, bone + 0x40u, 0,
                        em_a01m_lw(m, bone + 0x50u), NULL, NULL);
        if (em_area01_math_001C69A0_bone(m, model, bone) < 0) return -1;
    }
    return done(m);
}
