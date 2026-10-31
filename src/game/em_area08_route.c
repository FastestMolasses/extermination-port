/* AREA08's and AREA07 sub 0's overlay owners from byte-identical decomp C
 * (src/overlays/AREA08, AREA07; link addresses there, 0x40 below these). */
#include "em_area08_route_internal.h"
#define B(a) a08r_u8(o, (a))
#define R(a) a08r_u32(o, (a))
#define W(a,v) a08r_w32(o, (a), (v))
#define WB(a,v) a08r_w8(o, (a), (v))
#define WH(a,v) a08r_w16(o, (a), (v))
#define S16(a) a08r_s16(o, (a))
#define F(v) a08r_bits(v)
#define C(fn,...) do { if (a08r_c_##fn(o, __VA_ARGS__)) return; } while (0)
#define CV(fn,...) do { if (a08r_c_##fn(o, __VA_ARGS__)) return 0; } while (0)
#define ONE 0x3F800000u
#define PI 0x40490FDBu

/* A vector of four words into the scratchpad. */
static void vec4(A08R *o, uint32_t at, float x, float y, float z, float w)
{
    W(at, F(x)); W(at + 4, F(y)); W(at + 8, F(z)); W(at + 0xC, F(w));
}

/* overlay_AREA08_func_00823540.c: script 0x825880's other op09 callback. */
static int32_t fall_00823580(A08R *o, uint32_t self)
{
    uint32_t rec = R(self + 0x110);
    W(rec + 0x7C, A08R_SUB(R(rec + 0x7C), F(0.2f)));
    rec = R(self + 0x110);
    return A08R_LT(R(rec + 0x7C), F(-9.0f)) ? 1 : 0;
}

/* func_overlay_AREA08_008235A0.c: script 0x825880's op09 callback. */
static int32_t rise_008235E0(A08R *o, uint32_t self)
{
    uint32_t rec = R(self + 0x110);
    W(rec + 0x7C, A08R_ADD(R(rec + 0x7C), F(0.2f)));
    rec = R(self + 0x110);
    return A08R_LT(0u, R(rec + 0x7C)) ? 1 : 0;
}

/* func_overlay_AREA08_008235F0.c: door [6]'s Use while D_008107EF is clear. */
static int32_t use_00823630(A08R *o, uint32_t self, uint32_t who)
{
    uint32_t v = 0, s = 0, c = 0, y = 0;
    int32_t r = 0;
    if (!(B(self + 0xB) & 4)) return 0;
    CV(001B1240, self + 0xB0, R(0x810350), R(0x810358), &v);
    CV(001B1470, A08R_SUB(v, R(self + 0xC4)), &v);
    CV(0011DF78, v, &v);
    if (A08R_LE(v, F(1.5707964f))) {
        CV(001B1470, A08R_ADD(PI, R(self + 0xC4)), &y);
        W(0x810374, y);
        WH(self + 0x2E, 0);
    } else {
        CV(001B1470, R(self + 0xC4), &y);
        W(0x810374, y);
        WH(self + 0x2E, 1);
    }
    if (B(self + 3) == 0x16) WH(self + 0x2E, 1u - (uint32_t)S16(self + 0x2E));
    CV(0011E2A8, R(0x810374), &s);
    W(0x700038A0, A08R_SUB(R(self + 0xB0), A08R_MUL(F(6.0f), s)));
    W(0x700038A4, R(0x810354));
    CV(0011DE90, R(0x810374), &c);
    W(0x700038A8, A08R_SUB(R(self + 0xB8), A08R_MUL(F(6.0f), c)));
    W(0x700038AC, ONE);
    CV(00182F90, 0x8102B0, 0x700038A0);
    CV(001BA1A0, who, 0x825880);
    CV(001BA1F0, self, &r);
    return 1;
}

/* func_overlay_AREA08_00823770.c: the sound behind door [6] at entry 3. */
static void noise_008237B0(A08R *o, uint32_t self)
{
    int32_t x = 0;
    WH(self + 0x28, (uint32_t)(S16(self + 0x28) + 1));
    if (S16(self + 0x28) % 20) return;
    C(00122BB8, &x);
    x = a08r_sra((uint32_t)x, 16);
    x = a08r_sra((uint32_t)x * 3u, 15);
    if (S16(self + 0x2A) == x + 1 && B(0x81076F) == 0) {
        int32_t voice = 0;
        C(001FBD50, self, 0x906, 0, F(1000.0f), &voice);
    }
    x = S16(self + 0x2A);
    WH(self + 0x2A, x > 4 ? 0u : (uint32_t)(x + 1));
}

/* func_overlay_AREA08_00823830.c: door [6]. */
static void door_00823870(A08R *o, uint32_t self)
{
    const uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001BB520, self);
        WH(self + 0x28, 0); WH(self + 0x2A, 0); WB(self, 1);
        break;
    case 1: {
        uint32_t dx, dy, dz, ox, oy, oz, d = 0;
        if (B(0x8107EF) == 0) {
            switch (B(self + 5)) {
            case 0:
                if (B(0x810702) == 3) {
                    noise_008237B0(o, self);
                    if (a08r_failed(o)) return;
                }
                r = use_00823630(o, self, blk);
                if (a08r_failed(o)) return;
                if (r) WB(self + 5, B(self + 5) + 1);
                break;
            case 1:
                C(001BA1F0, self, &r);
                if (r) { WB(self + 5, 0); WB(self + 0xB, 0); }
                break;
            default: break;
            }
        } else {
            switch (B(self + 5)) {
            case 0:
                C(001BB560, self, blk, 0, &r);
                if (r) WB(self + 5, B(self + 5) + 1);
                break;
            case 1:
                C(001BB7C0, self, &r);
                if (r) WB(self + 5, B(self + 5) + 1);
                break;
            case 2:
                C(001BC150, self);
                WB(self + 5, B(self + 5) + 1);
                break;
            case 3:
                C(001BB7F0, self, &r);
                if (r) WB(self + 5, 0);
                break;
            default: break;
            }
        }
        C(001C6380, self);
        if (a08r_method(o, self)) return;
        dx = R(0x810350);
        ox = R(self + 0xB0);
        oy = R(self + 0xB4);
        oz = R(self + 0xB8);
        dy = R(0x810354);
        dz = R(0x810358);
        dx = A08R_SUB(dx, ox);
        dy = A08R_SUB(dy, oy);
        dz = A08R_SUB(dz, oz);
        C(0011E748, A08R_MADD(A08R_ADDA(A08R_MUL(dx, dx), A08R_MUL(dy, dy)), dz, dz), &d);
        if (A08R_LE(d, F(20.0f))) {
            WB(self + 1, 1);
            if (B(self + 2) & 0x80) C(001B1DE0, self);
        }
        break;
    }
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

/* One of the two point pairs by rand % 2, into D_700038A0 / D_700038B0. */
static void pair(A08R *o)
{
    int32_t r = 0;
    C(00122BB8, &r);
    switch (r % 2) {
    case 0:
        vec4(o, 0x700038A0, 340.0f, 222.0f, 160.0f, 1.0f);
        vec4(o, 0x700038B0, 360.0f, 222.0f, 300.0f, 1.0f);
        break;
    case 1:
        vec4(o, 0x700038A0, 240.0f, 222.0f, 155.0f, 1.0f);
        vec4(o, 0x700038B0, 360.0f, 222.0f, 175.0f, 1.0f);
        break;
    default: break;
    }
}

/* rand / 2^31 in the three lanes of D_700038C0. */
static void fraction(A08R *o)
{
    for (uint32_t k = 0; k < 3; ++k) {
        int32_t r = 0;
        C(00122BB8, &r);
        W(0x700038C0 + 4 * k, A08R_DIV(A08R_CVT_S_W(r), F(2147483648.0f)));
    }
    W(0x700038CC, ONE);
}

/* func_overlay_AREA08_00823A80.c: sub 2's emitter [0]. */
static void emitter_00823AC0(A08R *o, uint32_t self)
{
    const uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    uint32_t v = 0, obj = 0;
    switch (B(self + 4)) {
    case 0:
        C(00122BB8, &r);
        W(blk, (uint32_t)(r % 2 + 2));
        C(00122BB8, &r);
        W(blk + 4, (uint32_t)(r % 12 + 12));
        WB(self + 4, 1);
        if (B(0x8107F0) == 0xFF) { WB(self + 4, 3); break; }
        /* fall through */
    case 1:
        W(blk, R(blk) - 1u);
        if (a08r_s32(o, blk) < 0) {
            C(00122BB8, &r);
            W(blk, (uint32_t)(r % 2 + 2));
            pair(o);
            if (a08r_failed(o)) return;
            for (uint32_t k = 0, m = 0x700036A0; k < 2; ++k, m += 0x10) {
                fraction(o);
                if (a08r_failed(o)) return;
                C(001028D0, m, 0x700038B0, 0x700038A0);
                C(001028E8, m, m, 0x700038C0);
                C(001028B8, m, m, 0x700038A0);
            }
            C(001028D0, 0x700038A0, 0x700036B0, 0x700036A0);
            {
                uint32_t x = R(0x700038A0), y = R(0x700038A4), z = R(0x700038A8);
                C(0011E748, A08R_MADD(A08R_ADDA(A08R_MUL(x, x), A08R_MUL(y, y)), z, z), &v);
            }
            W(0x70003A20, v);
            C(00102760, 0x700038A0, 0x700038A0);
            C(001CD390, 0x700036E0, 0x700038A0);
            C(00102918, 0x700036E0, 0x700036E0, 0x700036A0);
            C(001EFEB0, (int32_t)0x8000003B, 0x700036E0, &obj);
            if (obj) {
                WB(obj + 5, 1);
                W(obj + 0x1F0, 12);
                W(obj + 0x1F4, R(0x70003A20));
                W(obj + 0x1F8, F(0.4f));
            }
        }
        W(blk + 4, R(blk + 4) - 1u);
        if (a08r_s32(o, blk + 4) < 0) {
            C(00122BB8, &r);
            W(blk + 4, (uint32_t)(r % 12 + 12));
            pair(o);
            if (a08r_failed(o)) return;
            fraction(o);
            if (a08r_failed(o)) return;
            C(001028D0, 0x700038D0, 0x700038B0, 0x700038A0);
            C(001028E8, 0x700038D0, 0x700038D0, 0x700038C0);
            C(001028B8, 0x700038D0, 0x700038D0, 0x700038A0);
            C(001029C0, 0x700036A0);
            C(00102B08, 0x700036A0, 0x700036A0, F(-0.34906587f));
            C(00122BB8, &r);
            C(00102BB0, 0x700036A0, 0x700036A0,
              A08R_SUB(A08R_MUL(F(6.2831855f), A08R_DIV(A08R_CVT_S_W(r), F(2147483648.0f))), PI));
            C(00102918, 0x700036A0, 0x700036A0, 0x700038D0);
            C(001EFEB0, (int32_t)0x80000042, 0x700036A0, &obj);
        }
        if (a08r_s32(o, 0x70003B68) % 100 == 0) {
            C(00122BB8, &r);
            switch (r % 4) {
            case 0: C(001FB9F0, 0x41D, 0x1000, 0x1000, 0x1000); break;
            case 1: C(001FB9F0, 0x41E, 0x1000, 0x1000, 0x1000); break;
            case 2: C(001FB9F0, 0x41F, 0x1000, 0x1000, 0x1000); break;
            default: break;
            }
        }
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

/* func_overlay_AREA08_00824450.c: [12]'s timeline at entry 6. */
static void timeline_00824490(A08R *o, uint32_t self)
{
    int32_t tick;
    WH(self + 0x28, (uint32_t)(S16(self + 0x28) + 1));
    if (S16(self + 0x28) == 20) C(001C67E0, self, 8, 0, 0);
    if (S16(self + 0x28) == 179) {
        W(self + 0xB0, F(305.8422f));
        W(self + 0xB4, F(220.0f));
        W(self + 0xB8, F(600.1731f));
        W(self + 0xC4, F(-0.890118f));
        C(001C67E0, self, 4, 0, 0);
    }
    if (S16(self + 0x28) == 2000) C(001C67E0, self, 7, F(20.0f), 0);
    if (S16(self + 0x28) == 2180) C(001C67E0, self, 0, F(20.0f), 0);
    tick = S16(self + 0x28);
    if (tick >= 3100) {
        if (tick == 3100) C(001C67E0, self, 0xA, F(20.0f), 0);
        W(self + 0xB0, A08R_SUB(R(self + 0xB0), F(0.07f)));
        W(self + 0xB8, A08R_ADD(R(self + 0xB8), F(0.07f)));
    }
}

/* func_overlay_AREA08_008241D0.c: [12] (counter 0x17). */
static void owner_00824210(A08R *o, uint32_t self)
{
    const uint32_t talk = self + 0x1F0;
    int32_t r = 0;
    uint32_t group = 0;
    switch (B(self + 4)) {
    case 0:
        C(001BA1C0, self, 0x17, &r);
        if (r) { WB(self + 4, 3); break; }
        C(001B10B0, self, (int32_t)B(self + 0xD), 0x62);
        C(001C63E0, self, 0);
        C(001CA6F0, self, 2);
        C(001BA8E0, self, (int32_t)B(self + 0xD));
        WB(self + 4, 1); WB(self, 1);
        break;
    case 1:
        if (!B(0x81076F)) break;
        if (B(0x8107EF) == 0) {
            switch (B(self + 5)) {
            case 0:
                C(001BA1A0, talk, 0x825AA0);
                WB(self + 5, 1);
                break;
            case 1:
                C(001BA1F0, self, &r);
                if (r) {
                    WH(self + 0x2E, 0xFFFF);
                    WB(self + 5, 0);
                    WB(0x8107EF, 1);
                    C(001B6660, 0x825420, &group);
                    W(0x8106C0, group);
                    C(001FAE70, 0);
                }
                break;
            default: break;
            }
        } else {
            switch (B(self + 5)) {
            case 0:
                if (B(0x810702) == 6) {
                    C(001BA1A0, talk, 0x825E60);
                    WB(self + 5, 1);
                    WH(self + 0x28, 0);
                }
                break;
            case 1:
                C(001BA580, self, (int32_t)B(self + 0xD));
                C(001BA1F0, self, &r);
                if (r) {
                    C(001C4760, 0x63, 1);
                    WB(0x8107EF, 0xFF);
                    WB(self + 4, 3);
                }
                timeline_00824490(o, self);
                if (a08r_failed(o)) return;
                break;
            default: break;
            }
        }
        C(001C64F0, self, ONE, &r);
        C(001B17A0, self);
        C(001C68C0, self);
        a08r_method(o, self);
        break;
    case 2: case 3:
        C(001BA540, self);
        C(001AFC10, self);
        break;
    default: break;
    }
}

/* The terminal owners' tables: AREA08's [2] (0x8245E0, flag 0x18) and AREA07's
 * [17] (0x823990, flag 0x15), the same C with these bytes and scripts. */
typedef struct {
    uint32_t list, flag, lit, other, counter, first, both, item;
} Terminal;
static const Terminal k_a08t = {0x827160, 0x18, 0x810770, 0x81076D, 0x8107F0, 0x826420, 0x8268E0, 0x13};
static const Terminal k_a07t = {0x827170, 0x15, 0x81076D, 0x810770, 0x8107ED, 0x8264B0, 0x826930, 0x11};

/* func_overlay_AREA08_00824750.c, func_overlay_AREA07_00823B00.c. */
static void half_first(A08R *o, uint32_t self, const Terminal *t)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        if (B(self + 0xB) & 4) {
            vec4(o, 0x700038A0, 0.0f, -6.0f, 0.0f, 1.0f);
            C(001B6F00, self, 0x700038A0, PI);
            C(001BA1A0, self + 0x1F0, t->first);
            WB(self + 5, 1);
        }
        break;
    case 1:
        C(001BA1F0, self, &r);
        if (r) {
            C(001C4760, (int32_t)t->item, 1);
            WB(t->counter, 0xFF);
        }
        break;
    default: break;
    }
}

/* func_overlay_AREA08_00824830.c, func_overlay_AREA07_00823BE0.c. */
static void half_both(A08R *o, uint32_t self, const Terminal *t)
{
    int32_t r = 0;
    switch (B(self + 5)) {
    case 0:
        if (B(self + 0xB) & 4) {
            C(001BA1A0, self + 0x1F0, t->both);
            vec4(o, 0x700038A0, 0.0f, -6.0f, 0.0f, 1.0f);
            C(001B6F00, self, 0x700038A0, PI);
            WB(self + 5, 1);
        }
        break;
    case 1:
        if (B(0x81077D) && !B(self + 6)) {
            vec4(o, 0x700038A0, 0.0f, -8.0f, 0.0f, 1.0f);
            C(001B6F00, self, 0x700038A0, PI);
            WB(self + 6, B(self + 6) + 1);
        }
        C(001BA1F0, self, &r);
        if (r) {
            C(001C4760, (int32_t)t->item, 1);
            C(001C4760, 0x14, 1);
            if (a08r_c_001FABB0(o)) return;
            C(001FB0B0, 9);
            WB(t->lit, 0xFF);
            WB(t->counter, 0xFF);
            WB(self + 5, 2);
        }
        break;
    default: break;
    }
}

/* func_overlay_AREA08_008245A0.c, func_overlay_AREA07_00823950.c. */
static void terminal(A08R *o, uint32_t self, const Terminal *t)
{
    int32_t r = 0;
    uint32_t lit;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) break;
        C(001C6380, self);
        W(self + 0x30, t->list);
        WB(self + 4, 1);
        C(001BA1C0, self, (int32_t)t->flag, &r);
        WB(self, r ? 2 : 1);
        break;
    case 1:
        lit = B(t->lit);
        if (lit != 0xFF) {
            if (lit == 0) {
                W(0x700038A0, A08R_ADD(R(self + 0xB0), F(0.4f)));
                W(0x700038A4, R(self + 0xB4));
                W(0x700038A8, R(self + 0xB8));
                W(0x700038AC, ONE);
                W(0x700038B0, 0); W(0x700038B4, 0x80); W(0x700038B8, 0); W(0x700038BC, 0x80);
                C(001F4BF0, 0x700038A0, 0x700038B0);
            }
            if (B(t->other) == 0xFF) half_both(o, self, t);
            else half_first(o, self, t);
            if (a08r_failed(o)) return;
        }
        C(001C6380, self);
        C(001B17A0, self);
        a08r_method(o, self);
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

static void half_00824790(A08R *o, uint32_t self) { half_first(o, self, &k_a08t); }
static void half_00824870(A08R *o, uint32_t self) { half_both(o, self, &k_a08t); }
static void terminal_008245E0(A08R *o, uint32_t self) { terminal(o, self, &k_a08t); }
static void half_a07_00823B40(A08R *o, uint32_t self) { half_first(o, self, &k_a07t); }
static void half_a07_00823C20(A08R *o, uint32_t self) { half_both(o, self, &k_a07t); }
static void terminal_a07_00823990(A08R *o, uint32_t self) { terminal(o, self, &k_a07t); }

/* func_overlay_AREA08_008249B0.c: sliders [27], [28], [29]. */
static void slider_008249F0(A08R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) break;
        C(001C6380, self);
        WB(self + 8, 3); WB(self, 1); WB(self + 4, 1);
        if (!A08R_LE(R(self + 0xB0), F(300.0f))) {
            W(self + 0x30, 0x275998);
            WB(self + 0x9E, 3);
        } else {
            W(self + 0x30, 0x275990);
        }
        break;
    case 1:
        if (!A08R_LE(R(self + 0xB0), F(300.0f))) {
            C(001B2140, self, &r);
            if (!r) break;
        }
        switch (B(self + 5)) {
        case 0:
            if (B(self + 0xB) & 4) {
                C(001BA1A0, self + 0x1F0, A08R_LT(R(self + 0xB0), F(300.0f)) ? 0x827170u : 0x827330u);
                WB(self + 5, 1);
            }
            break;
        case 1:
            C(001BA1F0, self, &r);
            if (r) { WB(self + 0xB, 0); WB(self + 5, 0); }
            break;
        default: break;
        }
        C(001B17A0, self);
        a08r_method(o, self);
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

static const struct { float x, y, z; uint32_t k; } k_flames[5] = {
    {137.0f, 225.0f, 156.0f, 0}, {142.0f, 225.0f, 169.0f, 0}, {154.0f, 225.0f, 164.0f, 0},
    {150.0f, 225.0f, 151.0f, 0}, {146.0f, 135.0f, 160.0f, 1}};

/* func_overlay_AREA08_00824B80.c: sub 3's flame columns [4]. */
static void flames_00824BC0(A08R *o, uint32_t self, uint32_t sp)
{
    const uint32_t packet = sp - 0x60u, hp = self + 0x2EC;
    uint32_t blk = self + 0x1F0;
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) break;
        C(001C6380, self);
        for (uint32_t k = 0, p = blk; k < 5; ++k, p += 4) {
            C(00122BB8, &r);
            W(p + 4, A08R_DIV(A08R_CVT_S_W(r), F(2147483648.0f)));
        }
        C(00122BB8, &r);
        W(blk, (uint32_t)r);
        WH(self + 0x28, 0);
        W(hp, 0xFFFFFFFFu);
        break;
    case 1: {
        uint32_t f, v = 0, rec, seed;
        int32_t idx, tick;
        rec = R(R(0x275B40) + 0xC);
        f = A08R_SUB(R(rec + 0x74), F(0.02f));
        W(0x70003A20, f);
        C(001B1470, f, &v);
        rec = R(R(0x275B40) + 0xC);
        W(rec + 0x74, v);
        C(001C6380, self);
        C(001B1B70, self);
        if (a08r_method(o, self)) return;
        idx = a08r_s32(o, hp);
#define TICK() (tick = S16(0x70003B8A), tick += a08r_s32(o, 0x70003B68), tick % 10 == 0)
        if (idx != -1) {
            if (R(0x281C30u + 4u * (uint32_t)idx) != 0x910) {
                C(0011A070, idx);
                W(0x281B70u + 4u * R(hp), 0xFFFFFFFFu);
                W(hp, 0xFFFFFFFFu);
            } else if (TICK()) {
                C(001FBE80, self, idx, F(100.0f), F(4096.0f), &r);
                W(hp, (uint32_t)r);
                if (r == -1) W(0x281B70u + 4u * R(hp), 0xFFFFFFFFu);
            }
        } else if (TICK()) {
            C(001FBD50, self, 0x910, 1, F(100.0f), &r);
            W(hp, (uint32_t)r);
            if (r != -1) W(0x281B70u + 4u * R(hp), 0x910);
        }
        seed = R(blk);
        for (uint32_t k = 0; k < 5; ++k, blk += 4) {
            int32_t h = 0;
            uint32_t s, phase;
            C(001029C0, 0x700036A0);
            vec4(o, 0x700036D0, k_flames[k].x, k_flames[k].y, k_flames[k].z, 1.0f);
            C(001CCF70, 0x700036D0, &h);
            s = A08R_CVT_S_W(seed >> 16 & 0xFFFF);
            s = A08R_DIV(s, F(65535.0f));
            s = A08R_ADD(s, F(0.0001f));
            seed = seed * 37u + 11u;
            C(001CFAE0, packet, 0, 0x700036A0, R(blk + 4), s, ONE, F(0.1f));
            C(001CFBE0, h, 1, 0x827DD0u + k_flames[k].k * 0x90u, packet, 0);
            phase = A08R_ADD(R(blk + 4), F(0.008f));
            W(blk + 4, phase);
            if (!A08R_LE(phase, F(2.0f))) W(blk + 4, A08R_SUB(R(blk + 4), ONE));
        }
        break;
    }
    default:
        C(001AFC10, self);
        break;
    }
}

/* func_overlay_AREA07_00823D60.c: AREA07's sliders [17], [18]. */
static void slider_a07_00823DA0(A08R *o, uint32_t self)
{
    int32_t r = 0;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) break;
        C(001C6380, self);
        WB(self, 1); WB(self + 4, 1);
        if (A08R_LT(R(self + 0xB8), F(600.0f))) {
            WB(self + 0x9E, 3);
            W(self + 0x30, 0x827180);
            WB(self + 8, 2);
        } else {
            W(self + 0x30, 0x275970);
            WB(self + 8, 3);
        }
        break;
    case 1:
        if (A08R_LT(R(self + 0xB8), F(600.0f))) {
            C(001B2140, self, &r);
            if (!r) break;
        }
        switch (B(self + 5)) {
        case 0:
            if (B(self + 0xB) & 4) {
                C(001BA1A0, self + 0x1F0, A08R_LT(R(0x810358), F(600.0f)) ? 0x8271A0u : 0x827360u);
                WB(self + 5, 1);
            }
            break;
        case 1:
            C(001BA1F0, self, &r);
            if (r) { WB(self + 0xB, 0); WB(self + 5, 0); }
            break;
        default: break;
        }
        C(001B17A0, self);
        a08r_method(o, self);
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

/* func_001BD370.c: the doors opening, the twin of 001BD270's closing: rec +0x10
 * and then +0x14 rise by 0.2, clamped at 5; the four pose floats +0x7C. */
static int32_t open_001BD370(A08R *o, uint32_t rec)
{
    uint32_t a = A08R_ADD(R(rec + 0x10), F(0.2f)), b, table, f10, f14;
    W(rec + 0x10, a);
    if (!A08R_LT(a, F(3.0f))) {
        if (!A08R_LT(a, F(5.0f))) W(rec + 0x10, F(5.0f));
        b = A08R_ADD(R(rec + 0x14), F(0.2f));
        W(rec + 0x14, b);
        if (!A08R_LE(b, F(5.0f))) {
            W(rec + 0x14, F(5.0f));
            return 1;
        }
    }
    table = R(0x275B40);
    f14 = R(rec + 0x14);
    W(R(table + 4) + 0x7C, A08R_NEG(f14));
    table = R(0x275B40);
    f10 = R(rec + 0x10);
    f14 = R(rec + 0x14);
    W(R(table + 8) + 0x7C, A08R_SUB(A08R_NEG(f10), f14));
    table = R(0x275B40);
    f10 = R(rec + 0x10);
    f14 = R(rec + 0x14);
    W(R(table + 0xC) + 0x7C, A08R_ADD(f10, f14));
    table = R(0x275B40);
    f14 = R(rec + 0x14);
    W(R(table + 0x10) + 0x7C, f14);
    return 0;
}

/* func_00157360.c: the switch's camera beat (script D_00246A20's op09). */
static int32_t beat_00157360(A08R *o, uint32_t self, uint32_t rec, uint32_t cam)
{
    uint32_t v, bit;
    switch (B(rec + 4)) {
    case 0:
        if (B(0x810700) == 7) {
            CV(00102948, 0x8105D0, cam + 0x20);
            CV(00102948, 0x8105E0, cam + 0x30);
            CV(00102948, 0x8101F0, cam + 0x20);
            CV(00102948, 0x810200, cam + 0x30);
        } else {
            W(0x8105D0, 0x43A78000u); W(0x8105D4, 0x436F0000u); W(0x8105D8, 0x44117333u); W(0x8105DC, ONE);
            W(0x8105E0, 0x43A78000u); W(0x8105E4, 0x436D0000u); W(0x8105E8, 0x4405C000u); W(0x8105EC, ONE);
            CV(00102948, 0x8101F0, 0x8105D0);
            CV(00102948, 0x810200, 0x8105E0);
        }
        W(cam + 0x10, 0);
        WB(rec + 4, B(rec + 4) + 1);
        break;
    case 1:
        v = A08R_ADD(R(cam + 0x10), ONE);
        W(cam + 0x10, v);
        if (A08R_LT(F(30.0f), v)) WB(rec + 4, B(rec + 4) + 1);
        break;
    case 2:
        v = B(0x810700);
        bit = B(self + 0x2E);
        WB(0x810841u + v, B(0x810841u + v) | 1u << (bit & 31u));
        CV(001FB9F0, 0x3EE, 0x1000, 0x1000, 0x1000);
        W(cam + 0x10, 0);
        WB(rec + 4, B(rec + 4) + 1);
        break;
    case 3:
        v = A08R_ADD(R(cam + 0x10), ONE);
        W(cam + 0x10, v);
        if (A08R_LT(F(60.0f), v)) WB(rec + 4, B(rec + 4) + 1);
        break;
    case 4:
        CV(001B0460, 1);
        return 1;
    default: break;
    }
    return 0;
}

/* func_00159FC0.c: the switch's Use. */
static int32_t use_00159FC0(A08R *o, uint32_t self, uint32_t who)
{
    int32_t r = 0;
    if (!(B(self + 0xB) & 4)) return 0;
    vec4(o, 0x700038A0, 0.3f, 0.0f, 5.0f, 1.0f);
    CV(001B6F00, self, 0x700038A0, 0x40490FDBu);
    CV(001BA1A0, who, 0x246A20);
    CV(001BA1F0, self, &r);
    WB(self, 2);
    return 1;
}

/* func_0015A070.c (byte-matched): the switch owner ([21], [28]). */
static void switch_0015A070(A08R *o, uint32_t self)
{
    int32_t r = 0;
    uint32_t area, bit;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r);
        if (r) return;
        C(001C6380, self);
        area = B(0x810700);
        bit = a08r_u16(o, self + 0x2E);
        if (B(0x810841u + area) & (1u << (bit & 31u))) {
            WB(self + 5, 2);
        } else {
            W(self + 0x30, 0x275468);
            WB(self, 1);
        }
        break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            C(00158590, self, 1, 1);
            C(00159FC0, self, self + 0x1F0, &r);
            if (r) WB(self + 5, B(self + 5) + 1);
            break;
        case 1:
            C(00158590, self, 1, 1);
            C(001BA1F0, self, &r);
            if (r) WB(self + 5, 2);
            break;
        case 2:
            C(00158590, self, 0, 1);
            WB(self, 2);
            break;
        default: break;
        }
        C(001B17A0, self);
        if (B(self + 3) == 6) a08r_method(o, self);
        break;
    case 2: case 3:
        C(001AFC10, self);
        break;
    default: break;
    }
}

#define ENTRY(name, call) \
    int em_area08_route_##name(const EmArea08RouteHooks *h, uint32_t self, EmArea08RouteFault *fault) \
    { A08R o; if (a08r_begin(&o, h, fault)) return -1; call(&o, self); return a08r_end(&o); }
ENTRY(0015A070, switch_0015A070)
ENTRY(008237B0, noise_008237B0)
ENTRY(00823870, door_00823870)
ENTRY(00823AC0, emitter_00823AC0)
ENTRY(00824210, owner_00824210)
ENTRY(00824490, timeline_00824490)
ENTRY(00824790, half_00824790)
ENTRY(00824870, half_00824870)
ENTRY(008245E0, terminal_008245E0)
ENTRY(008249F0, slider_008249F0)
ENTRY(a07_00823DA0, slider_a07_00823DA0)
ENTRY(a07_00823990, terminal_a07_00823990)
ENTRY(a07_00823B40, half_a07_00823B40)
ENTRY(a07_00823C20, half_a07_00823C20)

int em_area08_route_00824BC0(const EmArea08RouteHooks *h, uint32_t self, uint32_t sp, EmArea08RouteFault *fault)
{
    A08R o; if (a08r_begin(&o, h, fault)) return -1;
    flames_00824BC0(&o, self, sp); return a08r_end(&o);
}

int em_area08_route_00823580(const EmArea08RouteHooks *h, uint32_t self, int32_t *result, EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = fall_00823580(&o, self);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

int em_area08_route_008235E0(const EmArea08RouteHooks *h, uint32_t self, int32_t *result, EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = rise_008235E0(&o, self);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

int em_area08_route_001BD370(const EmArea08RouteHooks *h, uint32_t rec, int32_t *result, EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = open_001BD370(&o, rec);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

int em_area08_route_00157360(const EmArea08RouteHooks *h, uint32_t self, uint32_t rec, uint32_t cam, int32_t *result,
                             EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = beat_00157360(&o, self, rec, cam);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

int em_area08_route_00159FC0(const EmArea08RouteHooks *h, uint32_t self, uint32_t who, int32_t *result,
                             EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = use_00159FC0(&o, self, who);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

int em_area08_route_00823630(const EmArea08RouteHooks *h, uint32_t self, uint32_t who, int32_t *result,
                             EmArea08RouteFault *fault)
{
    A08R o; int32_t r;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    r = use_00823630(&o, self, who);
    if (a08r_failed(&o)) return -1;
    *result = r; return 0;
}

/* func_overlay_AREA08_00824420.c: script 0x825AA0's op09 callback. */
int em_area08_route_00824460(const EmArea08RouteHooks *h, int32_t *result, EmArea08RouteFault *fault)
{
    A08R o;
    if (!result || a08r_begin(&o, h, fault)) return -1;
    if (a08r_c_001FABB0(&o) || a08r_c_001FA790(&o, 0, 0x11)) return -1;
    *result = 1; return 0;
}
