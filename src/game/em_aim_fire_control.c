#include "em_aim_fire_control.h"
#include "em_ee_float.h"
#include <string.h>

typedef struct { const EmAimFireControl *bus; int fault; } State;
static uint32_t get(State *s, uint32_t address, size_t n)
{
    uint32_t v = 0;
    if (s->fault) return 0;
    const unsigned char *p = s->bus->map(s->bus->context, address, n, 0);
    if (!p) { s->fault = -1; return 0; }
    for (size_t i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static void put(State *s, uint32_t address, uint32_t v, size_t n)
{
    if (s->fault) return;
    unsigned char *p = s->bus->map(s->bus->context, address, n, 1);
    if (!p) { s->fault = -1; return; }
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)(v >> (8 * i));
}
static int32_t short_at(State *s, uint32_t a) { return (int16_t)get(s, a, 2); }
static int call(State *s, uint32_t entry, uint32_t a0, uint32_t a1, uint32_t a2,
                uint32_t f12, uint32_t f13, EmAimFireControlCall *out)
{
    EmAimFireControlCall f = {{a0, a1, a2, 0}, {f12, f13, 0, 0}, 0, 0};
    if (s->fault) return s->fault;
    if (!s->bus->call) return s->fault = -1;
    int status = s->bus->call(s->bus->context, entry, &f);
    if (status) return s->fault = status < 0 ? status : -1;
    if (out) *out = f;
    return 0;
}
#define G8(a) get(s, (a), 1)
#define G16(a) get(s, (a), 2)
#define G32(a) get(s, (a), 4)
#define P8(a,v) put(s, (a), (v), 1)
#define P16(a,v) put(s, (a), (v), 2)
#define P32(a,v) put(s, (a), (v), 4)
#define CALL(e,a,b,c,x,y,r) do { if (call(s,e,a,b,c,x,y,r)) return s->fault; } while (0)
#define ADD em_ee_add_bits
#define SUB em_ee_sub_bits
#define MUL em_ee_mul_bits
#define DIV em_ee_div_bits
#define LE em_ee_c_le_bits
#define LT em_ee_c_lt_bits
#define EQ em_ee_c_eq_bits
#define HALF UINT32_C(0x3F000000)
#define ONE UINT32_C(0x3F800000)

static int reset(State *s, uint32_t p)
{
    P8(p + 0x1F1, 2); P8(p + 0x318, 2); P8(p + 0x2F2, 0);
    if (G8(0x8106C7)) P8(0x8106C7, 0);
    return s->fault;
}
static int select_fire(State *s, uint32_t p, int32_t mode, int32_t *out)
{
    P8(p + 0x274, 1);
    uint32_t stance = G8(p + 5);
    uint32_t table = stance == 0x1D || stance == 0x1E ? 0x248B70 : 0x248C50;
    int32_t clip = short_at(s, G32(table + (uint32_t)mode * 4));
    EmAimFireControlCall ret;
    CALL(0x1C61D0, G32(p + 0x40), (uint32_t)clip, 0, 0, 0, &ret);
    P32(p + 0x2F4, em_ee_cvt_s_w_bits(ret.v0));
    if (G8(p + 0x275) != (uint32_t)mode) {
        P8(p + 0x275, (uint32_t)mode); P8(p + 7, 0); *out = 1;
    } else *out = 0;
    return s->fault;
}
static int switch_fire(State *s, uint32_t p, int32_t edge, int32_t *out)
{
    int next = -1;
    *out = 0;
    if (G8(0x810CA4) == 2) { if (edge == 0) next = 5; }
    else switch (G8(0x810CA6)) {
    case 0:
        if (edge == 0) {
            if (!G8(0x810D3C)) {
                P8(0x810D3C, 1);
                CALL(0x1FBD50, 0x8102B0, 0x179, 0, 0x43960000, 0, NULL);
                P8(0x8106C7, 1);
            } else {
                P8(0x810D3C, 0);
                if (G8(0x8106C7)) P8(0x8106C7, 0);
            }
        }
        break;
    case 2: if (edge == 0) next = 1; break;
    case 3: if (edge == 1) next = 2; break;
    case 1: if (edge == 0) next = 3; break;
    case 4: if (edge == 1) next = 4; break;
    default: break;
    }
    if (next >= 0) {
        EmAimFireControlCall ret;
        CALL(0x17A8B0, p, (uint32_t)next, 0, 0, 0, &ret);
        *out = em_ee_word_int(ret.v0);
    }
    return s->fault;
}
static int cycle_fire(State *s, uint32_t p, int32_t *out)
{
    *out = 0;
    if (!G16(p + 0x2E)) return s->fault;
    if (!G8(p + 0x275)) {
        if (G8(0x810CA4) == 2) P8(p + 0x275, 5);
        else switch (G8(0x810CA6)) {
        case 0: return s->fault;
        case 2: P8(p + 0x275, 1); break;
        case 3: P8(p + 0x275, 2); break;
        case 1: P8(p + 0x275, 3); break;
        case 4: P8(p + 0x275, 4); break;
        default: break;
        }
    } else P8(p + 0x275, 0);
    P8(p + 7, 0); *out = 1;
    return s->fault;
}
static int reload(State *s, int32_t mode, int32_t *out)
{
    *out = 1;
    if (mode == 0 || mode == 1) {
        int32_t reserve = short_at(s, 0x810CB4);
        if (reserve && (mode == 1 || !G8(0x810C62))) {
            P8(0x810C62, reserve < 30 ? G8(0x810CB4) : 30);
            *out = 0;
        }
    } else {
        uint32_t mag = G8(0x810C62);
        if (mag < 30) {
            int32_t reserve = short_at(s, 0x810CB4);
            if ((int32_t)mag < reserve) {
                P8(0x810C62, reserve < (int32_t)(30 - mag) ? G8(0x810CB4) : 30);
                *out = 0;
            }
        }
    }
    return s->fault;
}
static int draw(State *s, uint32_t p, int32_t mode)
{
    P8(p + 0x1F1, 1);
    if (!mode) P8(p + 0x318, 3);
    if (G8(p + 0x317)) P8(p + 0x317, 0);
    P8(p + 0x2F2, 1); P16(p + 0x2E, 1);
    CALL(0x1FBD50, p, 0x162, 0, 0x43960000, 0, NULL);
    if (!G8(0x810CA6) && G8(0x810D3C)) {
        CALL(0x1FBD50, 0x8102B0, 0x179, 0, 0x43960000, 0, NULL);
        P8(0x8106C7, 1);
    }
    return s->fault;
}
static int pose(State *s, uint32_t p)
{
    uint32_t stance = G8(p + 0x1F0), factor;
    uint32_t rates[4] = {0, 0, 0x3BA3D70A, 0};
    if (stance == 0x31 || stance == 0x34) {
        rates[1] = 0x3B23D70A; rates[3] = 0x3C75C28F; factor = ONE;
    } else { rates[1] = 0x3ADA740D; rates[3] = 0x3C23D70A; factor = 0x3FC00000; }
    EmAimFireControlCall ret;
    CALL(0x1B5DC0, G8(0x810E64), 0, 0, 0, 0, &ret);
    if (ret.v0) {
        if (ret.v0 > 3) return -1;
        uint32_t idx = ret.v0, angle, pitch;
        P8(p + 0x302, 1); pitch = G32(p + 0x278);
        if (EQ(pitch, HALF)) angle = ONE;
        else {
            uint32_t scaled = !LE(pitch, HALF)
                ? ADD(HALF, MUL(0x3F19999A, SUB(pitch, HALF)))
                : SUB(HALF, MUL(0x3F19999A, SUB(HALF, pitch)));
            P32(0x70003A20, scaled);
            CALL(0x11E2A8, 0, 0, 0, MUL(0x40490FDB, G32(0x70003A20)), 0, &ret);
            angle = ret.f0;
        }
        uint32_t step = DIV(rates[idx], angle);
        if (G8(p + 0x275) == 4) step = MUL(step, 0x3FC00000);
        if (G8(0x810E64) >= 0x80) {
            uint32_t yaw = ADD(G32(p + 0x27C), step); P32(p + 0x27C, yaw);
            if (LT(ONE, yaw)) {
                CALL(0x1B1470, 0, 0, 0, SUB(G32(p + 0xC4), MUL(SUB(yaw, ONE), factor)), 0, &ret);
                P32(p + 0xC4, ret.f0); P32(p + 0x27C, ONE);
            }
        } else {
            uint32_t yaw = SUB(G32(p + 0x27C), step); P32(p + 0x27C, yaw);
            if (LT(yaw, 0)) {
                CALL(0x1B1470, 0, 0, 0, ADD(G32(p + 0xC4), MUL(em_ee_neg_bits(yaw), factor)), 0, &ret);
                P32(p + 0xC4, ret.f0); P32(p + 0x27C, 0);
            }
        }
    }
    CALL(0x1B5DC0, G8(0x810E65), 0, 0, 0, 0, &ret);
    if (ret.v0) {
        if (ret.v0 > 3) return -1;
        P8(p + 0x302, 1);
        uint32_t step = rates[ret.v0];
        stance = G8(p + 0x1F0);
        if (stance == 0x31 || stance == 0x34) {
            uint32_t pitch = G32(p + 0x278);
            if (LE(pitch, 0x3E99999A)) step = MUL(step, 0x3FC00000);
            else if (!LT(pitch, 0x3F333333)) step = MUL(step, 0x3FC00000);
        }
        uint32_t limit = stance - 0x31 < 2u ? ONE : 0x3F400000;
        if (G8(p + 0x275) == 4) step = MUL(step, 0x3FE66666);
        if (G8(0x810E65) >= 0x80) {
            uint32_t pitch = ADD(G32(p + 0x278), DIV(step, 0x40000000));
            P32(p + 0x278, pitch);
            if (LT(limit, pitch)) P32(p + 0x278, limit);
        } else {
            uint32_t pitch = SUB(G32(p + 0x278), DIV(step, 0x40000000));
            P32(p + 0x278, pitch);
            if (LT(pitch, 0)) P32(p + 0x278, 0);
        }
    }
    return s->fault;
}
static void translation(State *s, uint32_t p)
{
    for (unsigned k = 0; k < 3; ++k) {
        uint32_t node = G32(G32(0x275B40) + 0x10);
        P32(p + 0x2D0 + 4 * k, G32(node + 0xC0 + 4 * k));
    }
}
static int matrix_tail(State *s, uint32_t p)
{
    uint32_t previous = G8(p + 0x1F0);
    CALL(0x17A130, p, 0, 0, 0, 0, NULL);
    if (previous != 0x33) {
        uint32_t stance = G8(p + 0x1F0);
        if (stance == 0x32 || stance == 0x35 || G8(p + 0x275) == 4 || G8(p + 0x2F2)) {
            CALL(0x102958, p + 0x2A0, G32(G32(0x275B40) + 0x10) + 0x90, 0, 0, 0, NULL);
        } else translation(s, p);
    }
    return s->fault;
}
static int holster(State *s, uint32_t p)
{
    uint32_t state = G8(p + 7);
    int done = 0;
    EmAimFireControlCall ret;
    switch (state) {
    case 0: {
        P8(p + 7, 1); P32(p + 0x2E0, G32(p + 0x27C)); P32(p + 0x2E4, G32(p + 0x278));
        P16(p + 0x28, 8);
        P32(p + 0x26C, DIV(SUB(HALF, G32(p + 0x27C)), 0x41000000));
        P32(p + 0x270, DIV(SUB(HALF, G32(p + 0x278)), 0x41000000));
        uint32_t link = G32(p + 0x20);
        CALL(0x11E620, 0, 0, 0, em_ee_neg_bits(G32(link + 0xC8)), G32(link + 0xC0), &ret);
        P32(0x70003A20, ret.f0);
        CALL(0x1B1470, 0, 0, 0, ADD(0x3FC90FDB, G32(0x70003A20)), 0, &ret);
        P32(p + 0x218, ret.f0);
        /* Original begins the countdown on the setup tick. */
    } /* fall through */
    case 1: {
        int32_t count = short_at(s, p + 0x28); P16(p + 0x28, (uint32_t)(count - 1));
        if (!count) {
            P8(p + 7, G8(p + 7) + 1); P32(p + 0x27C, HALF); P32(p + 0x278, HALF);
            uint32_t stance = G8(p + 5);
            uint32_t table = stance == 0x1D || stance == 0x1E ? 0x248B88 : 0x248C68;
            CALL(0x1749A0, p, (uint32_t)short_at(s, table + G8(p + 0x275) * 2), 0, 0, 0, NULL);
            CALL(0x1749A0, p, (uint32_t)short_at(s, table + 0x10 + G8(p + 0x275) * 2), 0, ONE, 0, NULL);
            CALL(0x1FBD50, p, 0x163, 0, 0x43960000, 0, NULL);
        } else {
            P32(p + 0x27C, ADD(G32(p + 0x27C), G32(p + 0x26C)));
            P32(p + 0x278, ADD(G32(p + 0x278), G32(p + 0x270)));
            if (matrix_tail(s, p)) return s->fault;
            done = 1;
        }
        break;
    }
    case 2:
        if (G32(p + 0x200) & 0x1000) {
            uint32_t held = G16(0x810E70);
            if ((held & G16(0x70003B7C)) || (held & G16(0x70003B7E))) {
                P8(p + 7, G8(p + 7) + 1);
                CALL(0x1FBD50, p, (uint32_t)short_at(s, 0x248680 + G8(p + 0x275) * 2), 0, 0x43960000, 0, NULL);
                P16(p + 0x28, 8);
                P32(p + 0x26C, DIV(SUB(G32(p + 0x2E0), HALF), 0x41000000));
                P32(p + 0x270, DIV(SUB(G32(p + 0x2E4), HALF), 0x41000000));
                uint32_t stance = G8(p + 5);
                uint32_t table = stance == 0x1D || stance == 0x1E ? 0x248B88 : 0x248C68;
                CALL(0x1749A0, p, (uint32_t)short_at(s, table + G8(p + 0x275) * 2), 0, 0, 0, NULL);
            } else {
                P8(p + 6, 0x65);
                switch (G8(p + 5)) {
                case 0x1D: P8(p + 0x1F0, 0x31); break;
                case 0x1E: P8(p + 0x1F0, 0x32); break;
                case 0x1F: P8(p + 0x1F0, 0x34); break;
                case 0x20: P8(p + 0x1F0, 0x35); break;
                default: break;
                }
                CALL(0x16F5D0, p, 0, 0, 0, 0, NULL);
            }
        }
        break;
    case 3: {
        int32_t count = short_at(s, p + 0x28); P16(p + 0x28, (uint32_t)(count - 1));
        if (!count) {
            uint32_t stance = G8(p + 5);
            int side = (G16(0x810E70) & G16(0x70003B7E)) != 0;
            if (stance == 0x1D || stance == 0x1E) {
                P8(p + 5, side ? 0x1E : 0x1D); P8(p + 0x1F0, side ? 0x32 : 0x31);
            } else { P8(p + 5, side ? 0x20 : 0x1F); P8(p + 0x1F0, side ? 0x35 : 0x34); }
            P8(p + 6, 2); P8(p + 7, 0);
            P32(p + 0x27C, G32(p + 0x2E0)); P32(p + 0x278, G32(p + 0x2E4));
        } else {
            P32(p + 0x27C, ADD(G32(p + 0x27C), G32(p + 0x26C)));
            P32(p + 0x278, ADD(G32(p + 0x278), G32(p + 0x270)));
        }
        if (matrix_tail(s, p)) return s->fault;
        done = 1;
        break;
    }
    default: break;
    }
    if (G8(p + 0x1F0) == 0x33) {
        if (!done) { CALL(0x1C6DA0, p, 0, 0, 0, 0, NULL); }
        translation(s, p);
    }
    return s->fault;
}
static int spin(State *s, uint32_t p, uint32_t rate)
{
    EmAimFireControlCall ret;
    if (LE(G32(p + 0xC0), 0) && G16(G32(p + 0x20) + 0x2E) != 1) {
        P32(p + 0xC0, 0); P32(p + 0x38, 0); return s->fault;
    }
    uint32_t value = ADD(G32(p + 0x38), 0x3E20D97C);
    P32(p + 0x38, value);
    CALL(0x1B1470, 0, 0, 0, value, 0, &ret); P32(p + 0x38, ret.f0);
    CALL(0x11DE90, 0, 0, 0, ret.f0, 0, &ret);
    value = ADD(G32(p + 0xC0), MUL(rate, ret.f0)); P32(p + 0xC0, value);
    CALL(0x1B1470, 0, 0, 0, value, 0, &ret); P32(p + 0xC0, ret.f0);
    CALL(0x1029C0, 0x700036A0, 0, 0, 0, 0, NULL);
    CALL(0x102B08, 0x700036A0, 0x700036A0, 0, G32(p + 0xC0), 0, NULL);
    CALL(0x102BB0, 0x700036A0, 0x700036A0, 0, G32(p + 0xC4), 0, NULL);
    CALL(0x102918, 0x700036A0, 0x700036A0, p + 0x290, 0, 0, NULL);
    P32(0x700038A0, 0); P32(0x700038A4, 0xC1A40000); P32(0x700038A8, 0); P32(0x700038AC, ONE);
    CALL(0x1026A0, p + 0xB0, 0x700036A0, 0x700038A0, 0, 0, NULL);
    return s->fault;
}
static uint32_t clamp_unit(uint32_t v)
{
    return LT(v, 0) ? 0 : LE(v, ONE) ? v : ONE;
}
static uint32_t squares(uint32_t x, uint32_t y)
{
    return em_ee_madd_bits(em_ee_mula_bits(x, x), y, y);
}
static int track_target(State *s, uint32_t p)
{
    if (!G8(p + 0x2F2)) return s->fault;
    uint32_t stance = G8(p + 5), yaw_gate, yaw_up, yaw_down, pitch_gate, pitch_up, pitch_down;
    if (stance == 0x1D || stance == 0x1E) {
        yaw_gate = 0x39C62E4D; yaw_up = 0x3F8600F3; yaw_down = 0xBF86050C;
        pitch_gate = 0x3FC8E126; pitch_up = 0x3FB2A6FC; pitch_down = 0x3FB2DA88;
    } else {
        yaw_gate = 0x3A1BB6AA; yaw_up = 0x3F85DDF4; yaw_down = 0xBF85ED63;
        pitch_gate = 0x3FC863FA; pitch_up = 0x3FB25F29; pitch_down = 0x3FB2F4F8;
    }
    uint32_t tmp = s->bus->temporary;
    if (!tmp) return -1;
    EmAimFireControlCall ret;
    CALL(0x183C40, G32(0x8106E0), tmp, 0, 0, 0, NULL);
    CALL(0x1B1240, G32(p + 0x20) + 0xA0, 0, 0, G32(tmp), G32(tmp + 8), &ret);
    CALL(0x1B1470, 0, 0, 0, SUB(ret.f0, G32(p + 0xC4)), 0, &ret);
    uint32_t a = ret.f0, link = G32(p + 0x20);
    CALL(0x11E620, 0, 0, 0, G32(link + 0xC0), G32(link + 0xC8), &ret);
    CALL(0x1B1470, 0, 0, 0, SUB(ret.f0, G32(p + 0xC4)), 0, &ret);
    uint32_t delta = MUL(HALF, DIV(SUB(a, ret.f0), LE(a, yaw_gate) ? yaw_down : yaw_up));
    uint32_t yaw = clamp_unit(LE(a, yaw_gate) ? ADD(G32(p + 0x27C), delta) : SUB(G32(p + 0x27C), delta));
    CALL(0x17A800, G32(p + 0x20) + 0xA0, tmp, 0, 0, 0, &ret);
    a = ret.f0; link = G32(p + 0x20);
    CALL(0x11E748, 0, 0, 0, squares(G32(link + 0xC0), G32(link + 0xC8)), 0, &ret);
    CALL(0x11E620, 0, 0, 0, ret.f0, G32(G32(p + 0x20) + 0xC4), &ret);
    CALL(0x1B1510, 0, 0, 0, ret.f0, 0, &ret);
    uint32_t pitch;
    if (LE(a, pitch_gate)) pitch = ADD(G32(p + 0x278), MUL(HALF, DIV(SUB(ret.f0, a), pitch_up)));
    else pitch = SUB(G32(p + 0x278), MUL(HALF, DIV(SUB(a, ret.f0), pitch_down)));
    pitch = clamp_unit(pitch);
    P32(0x700038A0, SUB(yaw, G32(p + 0x27C))); P32(0x700038A4, SUB(pitch, G32(p + 0x278)));
    P32(0x700038A8, 0); P32(0x700038AC, ONE);
    CALL(0x102760, 0x700038B0, 0x700038A0, 0, 0, 0, NULL);
    CALL(0x11E748, 0, 0, 0, squares(G32(0x700038A0), G32(0x700038A4)), 0, &ret);
    P32(0x70003A20, ret.f0);
    if (LE(ret.f0, 0x3CA3D70A)) { P32(p + 0x27C, yaw); P32(p + 0x278, pitch); }
    else {
        P32(p + 0x27C, ADD(G32(p + 0x27C), MUL(0x3CA3D70A, G32(0x700038B0))));
        P32(p + 0x278, ADD(G32(p + 0x278), MUL(0x3CA3D70A, G32(0x700038B4))));
    }
    return s->fault;
}
static int pitch_to_target(State *s, uint32_t origin, uint32_t target, int32_t *result)
{
    uint32_t x = SUB(G32(target), G32(origin));
    P32(0x70003A20, x);
    uint32_t z = SUB(G32(target + 8), G32(origin + 8));
    x = G32(0x70003A20);
    P32(0x70003A28, z);
    EmAimFireControlCall ret;
    CALL(0x11E748, 0, 0, 0, squares(x, z), 0, &ret);
    P32(0x70003A2C, ret.f0);
    uint32_t y = SUB(G32(target + 4), G32(origin + 4));
    uint32_t horizontal = G32(0x70003A2C);
    P32(0x70003A24, y);
    CALL(0x11E620, 0, 0, 0, horizontal, y, &ret);
    P32(0x70003A20, ret.f0);
    CALL(0x1B1510, 0, 0, 0, ret.f0, 0, &ret);
    *result = em_ee_word_int(ret.f0);
    return s->fault;
}
static int stick_ring(State *s, uint32_t value, int32_t *result)
{
    EmAimFireControlCall ret;
    CALL(0x11E860, (value & 0xFFu) - 0x80u, 0, 0, 0, 0, &ret);
    int32_t magnitude = em_ee_word_int(ret.v0);
    *result = magnitude < 0x31 ? 0 : magnitude < 0x59 ? 1 : magnitude < 0x7B ? 2 : 3;
    return s->fault;
}
int em_aim_fire_control_run(const EmAimFireControl *bus, uint32_t entry,
                            uint32_t actor, int32_t argument, uint32_t float_argument,
                            int32_t *result)
{
    if (!bus || !bus->map) return -1;
    State state = {bus, 0}, *s = &state;
    int32_t ignored;
    if (!result) result = &ignored;
    switch (entry) {
    case 0x17A800: return pitch_to_target(s, actor, (uint32_t)argument, result);
    case 0x1B5DC0: return stick_ring(s, actor, result);
    case 0x16F5D0: return reset(s, actor);
    case 0x17A8B0: return select_fire(s, actor, argument, result);
    case 0x17A970: return switch_fire(s, actor, argument, result);
    case 0x17AAD0: return cycle_fire(s, actor, result);
    case 0x17B300: return reload(s, argument, result);
    case 0x16F530: return draw(s, actor, argument);
    case 0x17ABA0: return pose(s, actor);
    case 0x16F600: return holster(s, actor);
    case 0x172860: return spin(s, actor, float_argument);
    case 0x17AF70: return track_target(s, actor);
    case 0x17B420: {
        int32_t count = short_at(s, 0x810CAC); *result = 1;
        if (count) { P16(0x810CAC, (uint32_t)(count - 1)); P16(0x810CAE, 100); *result = 0; }
        return s->fault;
    }
    default: return -1;
    }
}
