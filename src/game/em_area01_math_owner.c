/* em_area01_math_owner.c - AREA01 lane "math": translations of 001BB860,
 * 001BB560, 001C02E0, 001BF630, 001BFFD0, 001CB360, 001B9CF0, 001BBAE0 and
 * 001BBBF0 (em_area01_math_owner.h, docs/AREA01_MATH.md).
 *
 * Read from the decomp's C (byte-matched: 001BB560, 001BBAE0, 001B9CF0,
 * 001CB360; NEARMISS: 001BB860, 001BBBF0, 001BFFD0, 001C02E0; asm-only:
 * 001BF630) and, for the register arguments each callee receives, the
 * float operation order and the stores made in call delay slots, from the
 * original instructions. Every COP1 operation goes through
 * game/em_ee_float.h. Verified by tools/test_area01_math_reference.py. */
#include "game/em_area01_math_owner.h"

#include "game/em_ee_float.h"

#define F_00102738 0x00102738u
#define F_00102760 0x00102760u
#define F_001026A0 0x001026A0u
#define F_001028B8 0x001028B8u
#define F_001028D0 0x001028D0u
#define F_00102948 0x00102948u
#define F_0011DE90 0x0011DE90u
#define F_0011DF78 0x0011DF78u
#define F_0011E2A8 0x0011E2A8u
#define F_0011E748 0x0011E748u
#define F_00182F90 0x00182F90u
#define F_001AF890 0x001AF890u
#define F_001AFA90 0x001AFA90u
#define F_001AFC10 0x001AFC10u
#define F_001B10B0 0x001B10B0u
#define F_001B1240 0x001B1240u
#define F_001B12B0 0x001B12B0u
#define F_001B1470 0x001B1470u
#define F_001B17A0 0x001B17A0u
#define F_001B1DE0 0x001B1DE0u
#define F_001B6660 0x001B6660u
#define F_001BA1A0 0x001BA1A0u
#define F_001BA1F0 0x001BA1F0u
#define F_001BB520 0x001BB520u
#define F_001BB7C0 0x001BB7C0u
#define F_001BB7F0 0x001BB7F0u
#define F_001BBD60 0x001BBD60u
#define F_001BC150 0x001BC150u
#define F_001BF6B0 0x001BF6B0u
#define F_001BFF90 0x001BFF90u
#define F_001BFFD0 0x001BFFD0u
#define F_001C6380 0x001C6380u
#define F_001C68C0 0x001C68C0u
#define F_001C7420 0x001C7420u
#define F_001CB2C0 0x001CB2C0u
#define F_001D0C80 0x001D0C80u
#define F_001D0D40 0x001D0D40u
#define F_001D0D60 0x001D0D60u
#define F_001D1F80 0x001D1F80u
#define F_001D3F50 0x001D3F50u
#define F_001EFE00 0x001EFE00u
#define F_001FBD50 0x001FBD50u
#define ANIM_ADVANCE_TIME 0x001C64F0u
#define BONE_INIT_DEFAULT_1 0x001C62C0u
#define BONE_INIT_DEFAULT_2 0x001C63E0u

/* Data addresses the routines name. */
#define PLAYER 0x008102B0u

#define K_ONE 0x3F800000u
#define K_TWO 0x40000000u
#define K_FIVE 0x40A00000u
#define K_SIX 0x40C00000u
#define K_EIGHT 0x41000000u
#define K_TEN 0x41200000u
#define K_TWELVE 0x41400000u
#define K_THIRTEEN 0x41500000u
#define K_TWENTY 0x41A00000u
#define K_THIRTY 0x41F00000u
#define K_M_THIRTY 0xC1F00000u
#define K_300 0x43960000u
#define K_0_01 0x3C23D70Au
#define K_PI 0x40490FDBu
#define K_HALF_PI 0x3FC90FDBu

#define SPR(o) (0x70000000u + (o))

typedef EmA01Math M;

static int done(const M *m) { return em_a01m_faulted(m) ? -1 : 0; }
static int put(M *m, uint32_t *v0, uint32_t value)
{
    if (em_a01m_faulted(m)) return -1;
    if (v0) *v0 = value;
    return 0;
}
static void st4(M *m, uint32_t a, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    em_a01m_sw(m, a, x);
    em_a01m_sw(m, a + 4, y);
    em_a01m_sw(m, a + 8, z);
    em_a01m_sw(m, a + 12, w);
}
static uint32_t call1(M *m, uint32_t fn, uint32_t a0)
{
    uint32_t v0 = 0;
    em_a01m_call_i(m, fn, 1, a0, 0, 0, 0, &v0);
    return v0;
}
static uint32_t call2(M *m, uint32_t fn, uint32_t a0, uint32_t a1)
{
    uint32_t v0 = 0;
    em_a01m_call_i(m, fn, 2, a0, a1, 0, 0, &v0);
    return v0;
}
static uint32_t call3(M *m, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t v0 = 0;
    em_a01m_call_i(m, fn, 3, a0, a1, a2, 0, &v0);
    return v0;
}
static uint32_t callf(M *m, uint32_t fn, uint32_t f12)
{
    uint32_t f0 = 0;
    em_a01m_call_if(m, fn, 0, 0, 0, 0, 0, f12, NULL, &f0);
    return f0;
}
/* The behaviour-table method at node +0x4C, called with the node. */
static void method_4c(M *m, uint32_t node) { call1(m, em_a01m_lw(m, node + 0x4Cu), node); }

/* sqrt(dx*dx + dy*dy + dz*dz): two products, ACC = first + second, then
 * ACC + dz*dz, handed to 0011E748. */
static uint32_t length3(M *m, uint32_t dx, uint32_t dy, uint32_t dz)
{
    uint32_t acc = em_ee_adda_bits(em_ee_mul_bits(dx, dx), em_ee_mul_bits(dy, dy));
    return callf(m, F_0011E748, em_ee_madd_bits(acc, dz, dz));
}

/* ---- 001BB560 -------------------------------------------------------------
 * 0 unless node byte +0xB has bit 2. Then, with p = the player record
 * D_008102B0: a = 001B1470(001B1240(node + 0xB0, p.A0, p.A8) - node.C4);
 * when fabs(a) (0011DF78) <= pi/2: p.C4 = 001B1470(pi + node.C4), node
 * half +0x2E = 0; else p.C4 = 001B1470(node.C4), +0x2E = 1. Node kinds
 * (+3) 8 and 0x16 invert +0x2E (1 - it). SPR 0x700038A0 = (node.B0 -
 * 6 sin(p.C4), p.A4, node.B8 - 6 cos(p.C4), 1.0); 00182F90(p, 0x700038A0).
 * a2 == 0: 001BBD60(node, 0x0024D980), 001BA1A0(a1, 0x0024D900), and in area
 * 0x16 with bit 0x80 of the half +0x34 clear, the byte at
 * D_0024E140[area] + (+0x34 & 0x7F) * 4 + (+0x2E) sets (1) or clears (2) bit
 * 0x80 of D_008106C8. a2 != 0: 001BA1A0(a1, 0x0024DA40). Then
 * 001BA1F0(node); v0 = 1. */
int em_area01_math_001BB560(M *m, uint32_t node, uint32_t a1, uint32_t a2, uint32_t *v0)
{
    uint32_t f0 = 0, t, kind;
    if (!(em_a01m_lbu(m, node + 0xBu) & 4)) return put(m, v0, 0);
    {
        const uint32_t a[1] = {node + 0xB0u};
        const uint32_t f[2] = {em_a01m_lw(m, PLAYER + 0xA0u), em_a01m_lw(m, PLAYER + 0xA8u)};
        em_a01m_call(m, F_001B1240, a, 1, f, 2, NULL, &f0);
    }
    t = callf(m, F_001B1470, em_ee_sub_bits(f0, em_a01m_lw(m, node + 0xC4u)));
    t = callf(m, F_0011DF78, t);
    if (em_ee_c_le_bits(t, K_HALF_PI)) {
        em_a01m_sw(m, PLAYER + 0xC4u, callf(m, F_001B1470, em_ee_add_bits(K_PI, em_a01m_lw(m, node + 0xC4u))));
        em_a01m_sh(m, node + 0x2Eu, 0);
    } else {
        em_a01m_sw(m, PLAYER + 0xC4u, callf(m, F_001B1470, em_a01m_lw(m, node + 0xC4u)));
        em_a01m_sh(m, node + 0x2Eu, 1);
    }
    kind = em_a01m_lbu(m, node + 3u);
    if (kind == 8 || kind == 0x16) em_a01m_sh(m, node + 0x2Eu, 1u - em_a01m_lhu(m, node + 0x2Eu));
    t = callf(m, F_0011E2A8, em_a01m_lw(m, PLAYER + 0xC4u));
    em_a01m_sw(m, SPR(0x38A0), em_ee_sub_bits(em_a01m_lw(m, node + 0xB0u), em_ee_mul_bits(K_SIX, t)));
    em_a01m_sw(m, SPR(0x38A4), em_a01m_lw(m, PLAYER + 0xA4u));
    t = callf(m, F_0011DE90, em_a01m_lw(m, PLAYER + 0xC4u));
    em_a01m_sw(m, SPR(0x38A8), em_ee_sub_bits(em_a01m_lw(m, node + 0xB8u), em_ee_mul_bits(K_SIX, t)));
    em_a01m_sw(m, SPR(0x38AC), K_ONE);
    call2(m, F_00182F90, PLAYER, SPR(0x38A0));
    if (a2 == 0) {
        uint32_t area;
        call2(m, F_001BBD60, node, 0x0024D980u);
        call2(m, F_001BA1A0, a1, 0x0024D900u);
        area = em_a01m_lbu(m, 0x00810700u);
        if (area == 0x16 && !(em_a01m_lh(m, node + 0x34u) & 0x80)) {
            uint32_t q = em_a01m_lw(m, 0x0024E140u + 4u * (area & 0xFFu));
            q += (em_a01m_lbu(m, node + 0x34u) & 0x7Fu) * 4u;
            uint32_t idx = em_a01m_lbu(m, q + em_a01m_lhu(m, node + 0x2Eu));
            if (idx == 1) em_a01m_sw(m, 0x008106C8u, em_a01m_lw(m, 0x008106C8u) | 0x80u);
            else if (idx == 2) em_a01m_sw(m, 0x008106C8u, em_a01m_lw(m, 0x008106C8u) & ~0x80u);
        }
    } else {
        call2(m, F_001BA1A0, a1, 0x0024DA40u);
    }
    call1(m, F_001BA1F0, node);
    return put(m, v0, 1);
}

/* ---- 001BB860 -------------------------------------------------------------
 * State byte +4: 0 -> 001BB520(node), byte +0 = 1. 2 or 3 -> 001AFC10(node).
 * 1 -> sub-state +5: 0 -> 001BB560(node, node + 0x1F0, flag) as below;
 * 1 -> when 001BB7C0 answers nonzero, +0xB = 0 and +5 = 0; 2 -> when
 * 001BB7C0 answers nonzero, +5 += 1; 3 -> 001BC150(node), +5 += 1; 4 ->
 * when 001BB7F0(node) answers nonzero, +5 = 0. Sub-state 0: kinds (+3)
 * 0x16 / 0x17 / 0x3E test bit (s16 +0x34 & 31) of the byte
 * D_00810841[area]: set -> flag 0 and +5 = 2 on success; clear -> flag 1
 * and +5 += 1 on success. Other kinds: flag 0, +5 = 2 on success.
 * Then (state 1): 001C6380(node), the +0x4C method, and when the distance
 * from D_00810350..58 to node +0xB0..B8 is <= 20: +1 = 1 and, with bit 0x80
 * of +2, 001B1DE0(node). Other states: nothing. */
int em_area01_math_001BB860(M *m, uint32_t node)
{
    uint32_t st = em_a01m_lbu(m, node + 4u);
    if (st == 3 || st == 2) {
        call1(m, F_001AFC10, node);
        return done(m);
    }
    if (st == 0) {
        call1(m, F_001BB520, node);
        em_a01m_sb(m, node, 1);
        return done(m);
    }
    if (st != 1) return done(m);

    switch (em_a01m_lbu(m, node + 5u)) {
    case 4:
        if (call1(m, F_001BB7F0, node) != 0) em_a01m_sb(m, node + 5u, 0);
        break;
    case 3:
        call1(m, F_001BC150, node);
        em_a01m_sb(m, node + 5u, em_a01m_lbu(m, node + 5u) + 1u);
        break;
    case 2:
        if (call1(m, F_001BB7C0, node) != 0) em_a01m_sb(m, node + 5u, em_a01m_lbu(m, node + 5u) + 1u);
        break;
    case 1:
        if (call1(m, F_001BB7C0, node) != 0) {
            em_a01m_sb(m, node + 0xBu, 0);
            em_a01m_sb(m, node + 5u, 0);
        }
        break;
    case 0: {
        uint32_t kind = em_a01m_lbu(m, node + 3u), r = 0;
        if (kind == 0x16 || kind == 0x17 || kind == 0x3E) {
            uint32_t bits = em_a01m_lbu(m, 0x00810841u + em_a01m_lbu(m, 0x00810700u));
            uint32_t bit = 1u << ((uint32_t)em_a01m_lh(m, node + 0x34u) & 31u);
            if (bits & bit) {
                em_area01_math_001BB560(m, node, node + 0x1F0u, 0, &r);
                if (r != 0) em_a01m_sb(m, node + 5u, 2);
            } else {
                em_area01_math_001BB560(m, node, node + 0x1F0u, 1, &r);
                if (r != 0) em_a01m_sb(m, node + 5u, em_a01m_lbu(m, node + 5u) + 1u);
            }
        } else {
            em_area01_math_001BB560(m, node, node + 0x1F0u, 0, &r);
            if (r != 0) em_a01m_sb(m, node + 5u, 2);
        }
        break;
    }
    default:
        break;
    }
    call1(m, F_001C6380, node);
    method_4c(m, node);
    {
        uint32_t dx = em_ee_sub_bits(em_a01m_lw(m, 0x00810350u), em_a01m_lw(m, node + 0xB0u));
        uint32_t dy = em_ee_sub_bits(em_a01m_lw(m, 0x00810354u), em_a01m_lw(m, node + 0xB4u));
        uint32_t dz = em_ee_sub_bits(em_a01m_lw(m, 0x00810358u), em_a01m_lw(m, node + 0xB8u));
        uint32_t d = length3(m, dx, dy, dz);
        if (em_ee_c_le_bits(d, K_TWENTY)) {
            em_a01m_sb(m, node + 1u, 1);
            if (em_a01m_lbu(m, node + 2u) & 0x80u) call1(m, F_001B1DE0, node);
        }
    }
    return done(m);
}

/* ---- 001BF630 -------------------------------------------------------------
 * 0 when the scratchpad byte 0x70003B8D is nonzero. Else dx = player.A0 -
 * node.B0, dz = player.A8 - node.B8; d = 0011E748(ACC(dx*dx) + dz*dz);
 * v0 = d <= *(float *)*(tail + 0x18). */
int em_area01_math_001BF630(M *m, uint32_t player, uint32_t node, uint32_t tail, uint32_t *v0)
{
    uint32_t dx, dz, d, limit;
    if (em_a01m_lbu(m, SPR(0x3B8D)) != 0) return put(m, v0, 0);
    dx = em_ee_sub_bits(em_a01m_lw(m, player + 0xA0u), em_a01m_lw(m, node + 0xB0u));
    dz = em_ee_sub_bits(em_a01m_lw(m, player + 0xA8u), em_a01m_lw(m, node + 0xB8u));
    d = callf(m, F_0011E748, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
    limit = em_a01m_lw(m, em_a01m_lw(m, tail + 0x18u));
    return put(m, v0, em_ee_c_le_bits(d, limit) ? 1u : 0u);
}

/* ---- 001C02E0 -------------------------------------------------------------
 * `tail` = node + 0x1F0. State +4:
 * 0: with bit 0x20 of D_00810845: D_00810766 = 0xFF, behaviour (+0x10) =
 *    001BF6B0, 001B6660(0x829110, 2, 0), done. Else SPR 0x700038A0 = (0, 2,
 *    5, 1); c = 001AFA90(2, 2); none -> +4 = 3. With c: 00102948 copies of
 *    node +0xB0 / +0xC0 into c +0xB0 / +0xC0, c +3 = 0x12, c +0xD = 2,
 *    c +0x10 = 001BFFD0, c +0x20 = node, node +0x24 = c; 001D0C80(node,
 *    D_0028A518); 001D0D40(node, 0x0024FD50, 0x5B, 1); tail halves +2 and +0
 *    = 0; bone_init_default_1(node); node +0x58 = D_0028A51C, +0 = 3, +4 = 1,
 *    half +0x34 = 0x50, +0x30 = 0x00275638, tail +0x18 = 0x00275648;
 *    001C6380(node); then two probe directions built in SPR 0x700038A0..FF
 *    with 00102948 / 001026A0 / 001028D0 / 00102760 / 001028B8 (the +30 and
 *    -30 y offsets), tail +0xC..14 = the first, tail +8 = 00102738 of the
 *    two.
 * 1: sub-state +5 0 -> half +0x28 = 0 and +5 = 1, then as 1; 1 -> when
 *    001BF630(D_008102B0, node, tail): 001FBD50(node, 0x444, 0, 300.0)
 *    unless the scratchpad byte 0x70003B64 is set, half +0x28 = 1; else +0x28
 *    = 0. Then (any sub-state) a nonzero half +0x36 sets +0 = 3 and clears
 *    it; 001C6380(node); with +0x28 set, tail half +0 = 001D0D60(node +0x90
 *    word, 1.0); 001B17A0(node); the +0x4C method.
 * 2: +4 = 3.  3: 001AF890(node +0x90 word); 001AFC10(node). */
int em_area01_math_001C02E0(M *m, uint32_t node)
{
    uint32_t tail = node + 0x1F0u;
    uint32_t st = em_a01m_lbu(m, node + 4u);
    switch (st) {
    case 0: {
        uint32_t c;
        if (em_a01m_lbu(m, 0x00810845u) & 0x20u) {
            em_a01m_sb(m, 0x00810766u, 0xFF);
            em_a01m_sw(m, node + 0x10u, F_001BF6B0);
            call3(m, F_001B6660, 0x00829110u, 2, st);
            return done(m);
        }
        st4(m, SPR(0x38A0), 0, K_TWO, K_FIVE, K_ONE);
        c = call2(m, F_001AFA90, 2, 2);
        if (em_a01m_faulted(m)) return -1;
        if (c == 0) {
            em_a01m_sb(m, node + 4u, 3);
            return done(m);
        }
        call2(m, F_00102948, c + 0xB0u, node + 0xB0u);
        call2(m, F_00102948, c + 0xC0u, node + 0xC0u);
        em_a01m_sb(m, c + 3u, 0x12);
        em_a01m_sb(m, c + 0xDu, 2);
        em_a01m_sw(m, c + 0x10u, F_001BFFD0);
        em_a01m_sw(m, c + 0x20u, node);
        em_a01m_sw(m, node + 0x24u, c);
        call2(m, F_001D0C80, node, em_a01m_lw(m, 0x0028A518u));
        {
            const uint32_t a[4] = {node, 0x0024FD50u, 0x5B, 1};
            em_a01m_call(m, F_001D0D40, a, 4, NULL, 0, NULL, NULL);
        }
        em_a01m_sh(m, tail + 2u, 0);
        em_a01m_sh(m, tail, 0);
        call1(m, BONE_INIT_DEFAULT_1, node);
        em_a01m_sw(m, node + 0x58u, em_a01m_lw(m, 0x0028A51Cu));
        em_a01m_sb(m, node, 3);
        em_a01m_sb(m, node + 4u, 1);
        em_a01m_sh(m, node + 0x34u, 0x50);
        em_a01m_sw(m, node + 0x30u, 0x00275638u);
        em_a01m_sw(m, tail + 0x18u, 0x00275648u);
        call1(m, F_001C6380, node);
        call2(m, F_00102948, SPR(0x38A0), node + 0xB0u);
        st4(m, SPR(0x38B0), 0, K_THIRTY, 0, K_ONE);
        em_a01m_sw(m, SPR(0x38AC), K_ONE);
        call3(m, F_001026A0, SPR(0x38B0), node + 0xD0u, SPR(0x38B0));
        em_a01m_sw(m, SPR(0x38BC), K_ONE);
        call3(m, F_001028D0, SPR(0x38E0), SPR(0x38B0), SPR(0x38A0));
        em_a01m_sw(m, tail + 0xCu, em_a01m_lw(m, SPR(0x38E0)));
        em_a01m_sw(m, tail + 0x10u, em_a01m_lw(m, SPR(0x38E4)));
        em_a01m_sw(m, tail + 0x14u, em_a01m_lw(m, SPR(0x38E8)));
        call2(m, F_00102760, SPR(0x38E0), SPR(0x38E0));
        st4(m, SPR(0x38B0), 0, K_M_THIRTY, 0, K_ONE);
        call3(m, F_001028B8, SPR(0x38B0), SPR(0x38A0), SPR(0x38B0));
        em_a01m_sw(m, SPR(0x38BC), K_ONE);
        call3(m, F_001028D0, SPR(0x38F0), SPR(0x38B0), SPR(0x38A0));
        call2(m, F_00102760, SPR(0x38F0), SPR(0x38F0));
        {
            uint32_t f0 = 0;
            const uint32_t a[2] = {SPR(0x38E0), SPR(0x38F0)};
            em_a01m_call(m, F_00102738, a, 2, NULL, 0, NULL, &f0);
            em_a01m_sw(m, tail + 8u, f0);
        }
        return done(m);
    }
    case 1: {
        uint32_t sub = em_a01m_lbu(m, node + 5u);
        if (sub == 0) {
            em_a01m_sh(m, node + 0x28u, 0);
            em_a01m_sb(m, node + 5u, em_a01m_lbu(m, node + 5u) + 1u);
        }
        if (sub == 0 || sub == 1) {
            uint32_t r = 0;
            em_area01_math_001BF630(m, PLAYER, node, tail, &r);
            if (r != 0) {
                if (em_a01m_lbu(m, SPR(0x3B64)) == 0)
                    em_a01m_call_if(m, F_001FBD50, 3, node, 0x444, 0, 0, K_300, NULL, NULL);
                em_a01m_sh(m, node + 0x28u, 1);
            } else {
                em_a01m_sh(m, node + 0x28u, 0);
            }
        }
        if (em_a01m_lh(m, node + 0x36u) != 0) {
            em_a01m_sb(m, node, 3);
            em_a01m_sh(m, node + 0x36u, 0);
        }
        call1(m, F_001C6380, node);
        if (em_a01m_lh(m, node + 0x28u) != 0) {
            uint32_t v0 = 0;
            em_a01m_call_if(m, F_001D0D60, 1, em_a01m_lw(m, node + 0x90u), 0, 0, 0, K_ONE, &v0, NULL);
            em_a01m_sh(m, tail, v0);
        }
        call1(m, F_001B17A0, node);
        method_4c(m, node);
        return done(m);
    }
    case 2:
        em_a01m_sb(m, node + 4u, 3);
        return done(m);
    case 3:
        call1(m, F_001AF890, em_a01m_lw(m, node + 0x90u));
        call1(m, F_001AFC10, node);
        return done(m);
    default:
        return done(m);
    }
}

/* ---- 001BFFD0 (with its second piece 001C0004) ----------------------------
 * e0 = the node's +0x20 node, tails e2 = node + 0x1F0, e1 = e0 + 0x1F0.
 * State +4:
 * 0: e0 state 3 -> +4 = 3. Else when 001B10B0(node, 0x20, 0x21) is 0:
 *    bone_init_default_2(node, 0); +0x58 = D_0028A520; +0 = 1 when +0xD is
 *    1, else +0 = 3 and the word +0x80 of the +0x110 record = 0xC0900000;
 *    +4 = 1; e2 half +2 = 0; +0x30 = 0x00275638; e2 +0x1C = 0; half +0x34 =
 *    0x64.
 * 1: e0 state >= 2 -> +4 = 2. With e0 byte +1 set: +0xD == 2 -> advance
 *    the animation by 1.0 only when e0 half +0x28 is nonzero; else advance
 *    by 1.0, 001BFF90(node, e2, e1 half +2 != 0), e2 +0x1C counts down to
 *    0, and a nonzero half +0x36 while e0 +0 == 1 is handed over: with
 *    bits 0x5000 only when both tails' +0x1C are 0 (bit 0x4000 also starts
 *    effect 0x80000027 on the node; e2 +0x1C = 0x3C), then e0 +0x36 = it
 *    and e0 +0 = 3; +0x36 = 0 and +0 = 1 (also when e0 +0 != 1). Then
 *    001C68C0(node), 001B17A0(node), the +0x4C method.
 * 2: 001BFF90(node, e2, 0); +0x64 -= 0.01, below 0 -> 0 and +4 = 3;
 *    advance by 1.0; 001C68C0(node); the +0x4C method unless +0x64 is 0.
 * 3: 001AFC10(node). */
static void advance(M *m, uint32_t node)
{
    em_a01m_call_if(m, ANIM_ADVANCE_TIME, 1, node, 0, 0, 0, K_ONE, NULL, NULL);
}

int em_area01_math_001BFFD0(M *m, uint32_t node)
{
    uint32_t e0 = em_a01m_lw(m, node + 0x20u);
    uint32_t st = em_a01m_lbu(m, node + 4u);
    uint32_t e2 = node + 0x1F0u, e1 = e0 + 0x1F0u;
    switch (st) {
    case 0:
        if (em_a01m_lbu(m, e0 + 4u) == 3) {
            em_a01m_sb(m, node + 4u, 3);
            break;
        }
        if (call3(m, F_001B10B0, node, 0x20, 0x21) != 0) break;
        call2(m, BONE_INIT_DEFAULT_2, node, 0);
        em_a01m_sw(m, node + 0x58u, em_a01m_lw(m, 0x0028A520u));
        if (em_a01m_lbu(m, node + 0xDu) == 1) {
            em_a01m_sb(m, node, 1);
        } else {
            em_a01m_sb(m, node, 3);
            em_a01m_sw(m, em_a01m_lw(m, node + 0x110u) + 0x80u, 0xC0900000u);
        }
        em_a01m_sb(m, node + 4u, 1);
        em_a01m_sh(m, e2 + 2u, 0);
        em_a01m_sw(m, node + 0x30u, 0x00275638u);
        em_a01m_sw(m, e2 + 0x1Cu, 0);
        em_a01m_sh(m, node + 0x34u, 0x64);
        break;
    case 1:
        if (em_a01m_lbu(m, e0 + 4u) >= 2) em_a01m_sb(m, node + 4u, 2);
        if (em_a01m_lbu(m, e0 + 1u) == 0) break;
        if (em_a01m_lbu(m, node + 0xDu) == 2) {
            if (em_a01m_lh(m, e0 + 0x28u) != 0) advance(m, node);
        } else {
            int32_t flags;
            uint32_t t;
            advance(m, node);
            call3(m, F_001BFF90, node, e2, em_a01m_lh(m, e1 + 2u) == 0 ? 0u : 1u);
            t = em_a01m_lw(m, e2 + 0x1Cu);
            if (t != 0) em_a01m_sw(m, e2 + 0x1Cu, t - 1u);
            flags = em_a01m_lh(m, node + 0x36u);
            if (flags != 0) {
                if (em_a01m_lbu(m, e0) == 1) {
                    if (flags & 0x5000) {
                        if (em_a01m_lw(m, e2 + 0x1Cu) == 0 && em_a01m_lw(m, e1 + 0x1Cu) == 0) {
                            if (flags & 0x4000) call2(m, F_001EFE00, 0x80000027u, node);
                            em_a01m_sw(m, e2 + 0x1Cu, 0x3C);
                            em_a01m_sh(m, e0 + 0x36u, (uint32_t)em_a01m_lh(m, node + 0x36u));
                            em_a01m_sb(m, e0, 3);
                        }
                    } else {
                        em_a01m_sh(m, e0 + 0x36u, (uint32_t)flags);
                        em_a01m_sb(m, e0, 3);
                    }
                }
                em_a01m_sh(m, node + 0x36u, 0);
                em_a01m_sb(m, node, 1);
            }
        }
        call1(m, F_001C68C0, node);
        call1(m, F_001B17A0, node);
        method_4c(m, node);
        break;
    case 2: {
        uint32_t f;
        call3(m, F_001BFF90, node, e2, 0);
        f = em_ee_sub_bits(em_a01m_lw(m, node + 0x64u), K_0_01);
        em_a01m_sw(m, node + 0x64u, f);
        if (em_ee_c_lt_bits(f, 0)) {
            em_a01m_sw(m, node + 0x64u, 0);
            em_a01m_sb(m, node + 4u, 3);
        }
        advance(m, node);
        call1(m, F_001C68C0, node);
        if (!em_ee_c_eq_bits(0, em_a01m_lw(m, node + 0x64u))) method_4c(m, node);
        break;
    }
    case 3:
        call1(m, F_001AFC10, node);
        break;
    default:
        break;
    }
    return done(m);
}

/* ---- 001CB360 ------------------------------------------------------------- */
int em_area01_math_001CB360(M *m, uint32_t node)
{
    call3(m, F_001C7420, node, 0x3F5, 0);
    call3(m, F_001CB2C0, node, 0x3F3, 0);
    call3(m, F_001D1F80, 0, 1, 0);
    call1(m, F_001D3F50, em_a01m_lw(m, node + 0x44u));
    return done(m);
}

/* ---- 001B9CF0 -------------------------------------------------------------
 * n = op +8 (unsigned < 14, else 0). r = 001B12B0(target, current, op +0xC):
 *   0..2    node +0xC0 + 4n toward op +0x20 + 4n
 *   3       node +0xC0 / C4 / C8 toward op +0x20 / 24 / 28 (1 when all
 *           three arrived)
 *   4..6    node +0xB0 + 4n toward a1 +0x30 + 4n
 *   7..9    D_00810354[n] toward op +4 + 4n
 *   11..13  a1 byte +4: 0 -> +4 = 1, op +0x10 = 001B1470(node +0x94 + 4n +
 *           op[+4n - 0xC]); 1 -> node +0x94 + 4n toward op +0x10
 * v0 = 1 when the stepped value equals the target (float compare), else 0.
 * The lane index is re-read from op +8 after each call, as the original
 * does. */
static uint32_t step(M *m, uint32_t target, uint32_t current, uint32_t rate)
{
    uint32_t f0 = 0;
    const uint32_t f[3] = {target, current, rate};
    em_a01m_call(m, F_001B12B0, NULL, 0, f, 3, NULL, &f0);
    return f0;
}

int em_area01_math_001B9CF0(M *m, uint32_t node, uint32_t a1, uint32_t op, uint32_t *v0)
{
    uint32_t n = em_a01m_lw(m, op + 8u), o, r, cnt;
    if (em_a01m_faulted(m)) return -1;
    switch (n) {
    case 0: case 1: case 2:
        o = n * 4u;
        r = step(m, em_a01m_lw(m, op + 0x20u + o), em_a01m_lw(m, node + 0xC0u + o), em_a01m_lw(m, op + 0xCu));
        em_a01m_sw(m, node + 0xC0u + em_a01m_lw(m, op + 8u) * 4u, r);
        o = em_a01m_lw(m, op + 8u) * 4u;
        return put(m, v0, em_ee_c_eq_bits(em_a01m_lw(m, node + 0xC0u + o), em_a01m_lw(m, op + 0x20u + o)) ? 1u : 0u);
    case 7: case 8: case 9:
        o = n * 4u;
        r = step(m, em_a01m_lw(m, op + 4u + o), em_a01m_lw(m, 0x00810354u + o), em_a01m_lw(m, op + 0xCu));
        em_a01m_sw(m, 0x00810354u + em_a01m_lw(m, op + 8u) * 4u, r);
        o = em_a01m_lw(m, op + 8u) * 4u;
        return put(m, v0, em_ee_c_eq_bits(em_a01m_lw(m, 0x00810354u + o), em_a01m_lw(m, op + 4u + o)) ? 1u : 0u);
    case 3:
        cnt = 0;
        em_a01m_sw(m, node + 0xC0u, step(m, em_a01m_lw(m, op + 0x20u), em_a01m_lw(m, node + 0xC0u),
                                         em_a01m_lw(m, op + 0xCu)));
        if (em_ee_c_eq_bits(em_a01m_lw(m, node + 0xC0u), em_a01m_lw(m, op + 0x20u))) cnt = 1;
        em_a01m_sw(m, node + 0xC4u, step(m, em_a01m_lw(m, op + 0x24u), em_a01m_lw(m, node + 0xC4u),
                                         em_a01m_lw(m, op + 0xCu)));
        if (em_ee_c_eq_bits(em_a01m_lw(m, op + 0x24u), em_a01m_lw(m, node + 0xC4u))) cnt += 1;
        em_a01m_sw(m, node + 0xC8u, step(m, em_a01m_lw(m, op + 0x28u), em_a01m_lw(m, node + 0xC8u),
                                         em_a01m_lw(m, op + 0xCu)));
        if (em_ee_c_eq_bits(em_a01m_lw(m, op + 0x28u), em_a01m_lw(m, node + 0xC8u))) cnt += 1;
        return put(m, v0, cnt == 3 ? 1u : 0u);
    case 4: case 5: case 6:
        o = n * 4u;
        r = step(m, em_a01m_lw(m, a1 + 0x30u + o), em_a01m_lw(m, node + 0xB0u + o), em_a01m_lw(m, op + 0xCu));
        em_a01m_sw(m, node + 0xB0u + em_a01m_lw(m, op + 8u) * 4u, r);
        o = em_a01m_lw(m, op + 8u) * 4u;
        return put(m, v0, em_ee_c_eq_bits(em_a01m_lw(m, a1 + 0x30u + o), em_a01m_lw(m, node + 0xB0u + o)) ? 1u : 0u);
    case 11: case 12: case 13: {
        uint32_t sub = em_a01m_lbu(m, a1 + 4u);
        if (sub == 0) {
            em_a01m_sb(m, a1 + 4u, sub + 1u);
            o = em_a01m_lw(m, op + 8u) * 4u;
            r = em_ee_add_bits(em_a01m_lw(m, node + 0x94u + o), em_a01m_lw(m, op + o - 0xCu));
            em_a01m_sw(m, op + 0x10u, callf(m, F_001B1470, r));
            return put(m, v0, 0);
        }
        if (sub == 1) {
            o = n * 4u;
            r = step(m, em_a01m_lw(m, op + 0x10u), em_a01m_lw(m, node + 0x94u + o), em_a01m_lw(m, op + 0xCu));
            em_a01m_sw(m, node + 0x94u + em_a01m_lw(m, op + 8u) * 4u, r);
            o = em_a01m_lw(m, op + 8u) * 4u;
            return put(m, v0, em_ee_c_eq_bits(em_a01m_lw(m, node + 0x94u + o), em_a01m_lw(m, op + 0x10u)) ? 1u : 0u);
        }
        return put(m, v0, 0);
    }
    default:
        return put(m, v0, 0);
    }
}

/* ---- 001BBAE0 -------------------------------------------------------------
 * st[4] == 0: D_002821B0 = 2, D_002821B4 = 1; kind = signed byte node +0x56
 * & 0x3F; kinds 0..5 select D_002821B8 = 0x80000006 / 0x80000000 /
 * 0x80000002 / 0x80000008 / 0x8000000A / 0x80000004, D_002821BC = 0,
 * st[4] = 1, then the poll; other kinds return 1 at once. st[4] == 1: the
 * poll: 1 when D_002821B4 == 2, else 0. Other st[4]: 0. */
int em_area01_math_001BBAE0(M *m, uint32_t node, uint32_t st, uint32_t *v0)
{
    static const uint32_t selector[6] = {0x80000006u, 0x80000000u, 0x80000002u,
                                         0x80000008u, 0x8000000Au, 0x80000004u};
    uint32_t latch = em_a01m_lbu(m, st + 4u), kind;
    if (latch == 0) {
        em_a01m_sw(m, 0x002821B0u, 2);
        em_a01m_sw(m, 0x002821B4u, 1);
        kind = (uint32_t)em_a01m_lb(m, node + 0x56u) & 0x3Fu;
        if (kind >= 6) return put(m, v0, 1);
        em_a01m_sw(m, 0x002821B8u, selector[kind]);
        em_a01m_sw(m, 0x002821BCu, 0);
        em_a01m_sb(m, st + 4u, 1);
    } else if (latch != 1) {
        return put(m, v0, 0);
    }
    return put(m, v0, em_a01m_lw(m, 0x002821B4u) == 2 ? 1u : 0u);
}

/* ---- 001BBBF0 -------------------------------------------------------------
 * SPR 0x700038A0 = (node.B0 - 8 cos(node.C4), 10 + node.B4, node.B8 +
 * 8 sin(node.C4), 1.0); 00102948(D_008105E0, 0x700038A0); then with
 * a = D_00810374: x -= 13 sin(a), y = 12 + node.B4, z -= 13 cos(a);
 * 00102948(D_008105D0, 0x700038A0). v0 = 1. */
int em_area01_math_001BBBF0(M *m, uint32_t node, uint32_t *v0)
{
    uint32_t t;
    t = callf(m, F_0011DE90, em_a01m_lw(m, node + 0xC4u));
    em_a01m_sw(m, SPR(0x38A0), em_ee_sub_bits(em_a01m_lw(m, node + 0xB0u), em_ee_mul_bits(K_EIGHT, t)));
    em_a01m_sw(m, SPR(0x38A4), em_ee_add_bits(K_TEN, em_a01m_lw(m, node + 0xB4u)));
    t = callf(m, F_0011E2A8, em_a01m_lw(m, node + 0xC4u));
    em_a01m_sw(m, SPR(0x38A8), em_ee_add_bits(em_a01m_lw(m, node + 0xB8u), em_ee_mul_bits(K_EIGHT, t)));
    em_a01m_sw(m, SPR(0x38AC), K_ONE);
    call2(m, F_00102948, 0x008105E0u, SPR(0x38A0));
    t = callf(m, F_0011E2A8, em_a01m_lw(m, 0x00810374u));
    em_a01m_sw(m, SPR(0x38A0), em_ee_sub_bits(em_a01m_lw(m, SPR(0x38A0)), em_ee_mul_bits(K_THIRTEEN, t)));
    em_a01m_sw(m, SPR(0x38A4), em_ee_add_bits(K_TWELVE, em_a01m_lw(m, node + 0xB4u)));
    t = callf(m, F_0011DE90, em_a01m_lw(m, 0x00810374u));
    em_a01m_sw(m, SPR(0x38A8), em_ee_sub_bits(em_a01m_lw(m, SPR(0x38A8)), em_ee_mul_bits(K_THIRTEEN, t)));
    call2(m, F_00102948, 0x008105D0u, SPR(0x38A0));
    return put(m, v0, 1);
}
