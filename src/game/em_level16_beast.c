/* The AREA04 sub 1 creature (kind 7 nodes, behaviour 00147390) and its state
 * family. UNWIRED. From the decomp C; where that C is a NEARMISS (00148B40,
 * 0014B7B0, 0014BC30, 0014C0B0) or leaves a load order open, the original
 * instructions decide: every load, store, 00122BB8 (rand) and 001FBD50 call
 * below happens in the original's order, and the oracle compares it access
 * by access. A test written !(x <= y) is the original's compare, kept as is.
 *
 * self is the 0x2F0-byte pool node, st its work block at self + 0x1F0. The
 * player's position words are at 0x810360. Calls between the functions here
 * run directly; everything else is a hook. */
#include "em_level16_beast_internal.h"
#define B(a) l16b_u8(o,(a))
#define SB(a) l16b_s8(o,(a))
#define H(a) l16b_u16(o,(a))
#define SH(a) l16b_s16(o,(a))
#define R(a) l16b_u32(o,(a))
#define W(a,v) l16b_w32(o,(a),(v))
#define WB(a,v) l16b_w8(o,(a),(v))
#define WH(a,v) l16b_w16(o,(a),(v))
#define F(v) l16b_bits(v)
#define ADD(a,b) L16B_ADD((a),(b))
#define SUB(a,b) L16B_SUB((a),(b))
#define MUL(a,b) L16B_MUL((a),(b))
#define DIV(a,b) L16B_DIV((a),(b))
#define LT(a,b) L16B_LT((a),(b))
#define LE(a,b) L16B_LE((a),(b))
#define EQ(a,b) L16B_EQ((a),(b))
#define C(fn,...) do { if (l16b_c_##fn(o, __VA_ARGS__)) return 0; } while (0)
#define MAT 0x700036A0u   /* scratch matrix */
#define MAT_T 0x700036D0u /* its translation row */
#define VA 0x700038A0u    /* scratch vectors */
#define VB 0x700038B0u
#define VC 0x700038C0u
#define VD 0x700038D0u
#define OUT0 0x70003A20u  /* scratch floats */
#define OUT1 0x70003A24u
#define PLAYER 0x008102B0u
#define TARGET 0x00810360u
#define MODE 0x70003B8Du
#define CONTACT 0x700031D0u /* the last probe's surface record */
#define FLAG_70A 0x0081070Au
#define FLAG_6BD 0x008106BDu
#define FLAG_4E4 0x008104E4u
#define PI 0x40490FDBu
#define TAU 0x40C90FDBu
#define PI2 0x3FC90FDBu
#define PI4 0x3F490FDBu
#define ONE 0x3F800000u
#define NEG_ONE 0xBF800000u
#define RANGE 0x43960000u /* 300.0: the float argument of every 001FBD50 call */
#define POSE_A 0x00288D40u /* the three pose buffers of 00148B40 */
#define POSE_B 0x00287F40u
#define POSE_C 0x00287140u
#define POSE_D 0x00286340u

static uint32_t e001475A0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e00147700(L16B *o, uint32_t self, uint32_t st, uint32_t sp);
static uint32_t e00147960(L16B *o, uint32_t self, uint32_t st);
static uint32_t e00147B50(L16B *o, uint32_t self, uint32_t st, uint32_t sp);
static uint32_t e00148130(L16B *o, uint32_t self, uint32_t st);
static uint32_t e00148400(L16B *o, uint32_t self, uint32_t st);
static uint32_t e00148520(L16B *o, uint32_t self, uint32_t st);
static uint32_t e001486E0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e00148B40(L16B *o, uint32_t e, uint32_t st);
static uint32_t e00149B50(L16B *o, uint32_t self, uint32_t st, uint32_t sp);
static uint32_t e0014A0D0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014A350(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014A620(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014A730(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014A9A0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014ACC0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014AE50(L16B *o, uint32_t self);
static uint32_t e0014AE80(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014AFA0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014B7B0(L16B *o, uint32_t self, uint32_t st, uint32_t sp);
static uint32_t e0014BB10(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014BC30(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014BE20(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014C0B0(L16B *o, uint32_t self, uint32_t st);
static uint32_t e0014C130(L16B *o, uint32_t self);

/* The node's behaviour. The mode byte 0x70003B8D: 2 and 3 do nothing; 1 runs
 * only the +0x4C method, when 001B2140 passes and +4 is set; every other value
 * updates, dispatching on +4 (3 returns after its handler, without the timer
 * decrements; values above 3 run no handler). Frame 0x30. */
static uint32_t e00147390(L16B *o, uint32_t self, uint32_t sp)
{
    const uint32_t st = self + 0x1F0;
    uint32_t mode = B(MODE), phase, distance = 0;
    int32_t r = 0, v;
    if (mode == 3 || mode == 2) return 0;
    if (mode == 1) {
        C(001B2140, self, &r);
        if (!r || !B(self + 4)) return 0;
        (void)l16b_method(o, self);
        return 0;
    }
    WH(self + 0x52, 0);
    C(001B0D80, self);
    C(00102948, VA, self + 0xB0);
    C(00102948, VB, TARGET);
    W(VB + 4, 0);
    W(VA + 4, 0);
    C(001B15D0, VA, VB, &distance);
    W(st + 0x68, distance);
    phase = B(self + 4);
    if (phase == 3) { (void)e0014AE50(o, self); return 0; }
    if (phase == 2) (void)e0014A620(o, self, st);
    else if (phase == 1) (void)e00147700(o, self, st, sp - 0x30);
    else if (phase == 0) (void)e001475A0(o, self, st);
    v = (int32_t)R(st + 0x38);
    if (v) W(st + 0x38, (uint32_t)v - 1);
    v = SB(st + 0x8C);
    if (v) WB(st + 0x8C, (uint32_t)v - 1);
    v = SH(st + 0x74);
    if (v) WH(st + 0x74, (uint32_t)v - 1);
    v = SH(st + 0x70);
    if (v) WH(st + 0x70, (uint32_t)v - 1);
    v = SB(st + 0x89);
    if (v) WB(st + 0x89, (uint32_t)v - 1);
    v = SB(st + 0x8E);
    if (v) WB(st + 0x8E, (uint32_t)v - 1);
    v = SB(st + 0x85);
    if (v) WB(st + 0x85, (uint32_t)v - 1);
    WH(self + 0x54, 0);
    if (SB(st + 0x8A)) C(001B5360, self);
    return 0;
}

/* +4 == 0: the first frame. Copies +0xB0 and +0xC0 into st, sets +0x34 (with
 * +0xD bit 0x80, which D_00810808 == 0xFF sets: 400 or 250, else 250 or 180,
 * the larger when 0x81070A is set) and calls 001B10B0; a zero result then
 * runs 001C63E0. */
static uint32_t e001475A0(L16B *o, uint32_t self, uint32_t st)
{
    int32_t r = 0;
    WB(self + 4, B(self + 4) + 1);
    WB(self, 1);
    WB(st + 0x8A, 1);
    W(st + 0x40, ONE);
    WB(st + 0x8B, B(self + 0xD));
    W(self + 0x30, 0x002753F8);
    WB(st + 0x83, 0);
    C(00102948, st, self + 0xB0);
    C(00102948, st + 0x10, self + 0xC0);
    W(self + 0x58, R(0x0028A694));
    if (B(0x00810808) == 0xFF) WB(self + 0xD, B(self + 0xD) | 0x80);
    if (B(self + 0xD) & 0x80) {
        WH(self + 0x34, B(FLAG_70A) ? 0x190 : 0xFA);
        W(self + 0x60, 0x3F933333);
        W(self + 0x64, 0x3F933333);
        W(self + 0x68, 0x3F933333);
        W(self + 0x6C, ONE);
        C(001B10B0, self, 0x83, 0x84, &r);
    } else {
        WH(self + 0x34, B(FLAG_70A) ? 0xFA : 0xB4);
        C(001B10B0, self, 0x82, 0x84, &r);
    }
    if (!r) C(001C63E0, self, 0);
    return 0;
}

/* +4 == 1: the handlers by +5 (the ten-entry table at 0x26D2C0; +5 above 9
 * runs none), then the common per-frame work. */
static uint32_t e00147700(L16B *o, uint32_t self, uint32_t st, uint32_t sp)
{
    int32_t r = 0;
    WB(st + 0x8A, 0);
    C(001B2140, self, &r);
    if (!r) return 0;
    WB(st + 0x8A, 1);
    switch (B(self + 5)) {
    case 0: (void)e00147960(o, self, st); break;
    case 1: (void)e00147B50(o, self, st, sp - 0x30); break;
    case 2: (void)e00148130(o, self, st); break;
    case 3: (void)e00148400(o, self, st); break;
    case 4: (void)e00148520(o, self, st); break;
    case 5: (void)e001486E0(o, self, st); break;
    case 6: (void)e00148B40(o, self, st); break;
    case 7: (void)e00149B50(o, self, st, sp - 0x30); break;
    case 8: (void)e0014A0D0(o, self, st); break;
    case 9: (void)e0014A350(o, self, st); break;
    default: break;
    }
    WB(self, SB(st + 0x85) ? 3 : 1);
    if (SB(st + 0x86)) {
        WB(self + 0x5E, 0);
        WH(self + 0x52, H(self + 0x52) | 1);
    } else {
        WB(self + 0x5E, 1);
    }
    if (SB(st + 0x88)) {
        WB(self + 0x5D, 0x81);
        C(001B4810, self);
    } else {
        WB(self + 0x5D, 1);
    }
    WB(self + 0xA, 0);
    (void)e0014C0B0(o, self, st);
    (void)e0014BE20(o, self, st);
    (void)e0014AE80(o, self, st);
    (void)e0014AFA0(o, self, st);
    if (SB(st + 0x83)) {
        C(00131ED0, self);
    } else {
        C(001C64F0, self, R(st + 0x40), &r);
        W(st + 0x20, (uint32_t)r);
        if (B(st + 0x20) == 9) C(001FBD50, self, 0x7E6, 0, RANGE);
        C(00131ED0, self);
        C(001C68C0, self);
    }
    C(001B17A0, self, &r);
    (void)l16b_method(o, self);
    return 0;
}

/* +5 == 0. Case 0 rolls a code from the byte table 0x245B20 (two rows of 16,
 * row 1 while +0x56 is set): code 0 counts st+0x28 down in case 1, code 1
 * starts clip 1 and waits in case 2, code 2 sets +5 = 1. The original table
 * holds no other code. */
static uint32_t e00147960(L16B *o, uint32_t self, uint32_t st)
{
    int32_t r = 0, s;
    uint32_t state = B(self + 6), row, code;
    if (state == 2) {
        if (R(st + 0x20) & 0x1000) WB(self + 6, 0);
    } else if (state == 1) {
        uint32_t v = R(st + 0x28);
        if (v) W(st + 0x28, v - 1);
        else WB(self + 6, 0);
    } else if (state == 0) {
        s = SH(self + 0x56);
        W(st + 0x50, 0);
        row = s != 0;
        W(st + 0x4C, 0);
        C(00122BB8, &r);
        code = B(0x00245B20 + (row << 4) + (((uint32_t)r >> 7) & 0xF));
        if (code == 2) {
            WB(self + 5, 1);
            WB(self + 6, 0);
        } else if (code == 1) {
            WB(self + 6, 2);
            C(001C67E0, self, 1, F(5.0f), 0);
        } else if (code == 0) {
            WB(self + 6, 1);
            C(00122BB8, &r);
            W(st + 0x28, (((uint32_t)r >> 18) & 0x7F) + 0x78);
            if ((uint32_t)SH(self + 0x2C) & 0xFFFF7FFFu) C(001C67E0, self, 0, F(5.0f), 0);
        }
    }
    s = SH(self + 0x56);
    if (s) {
        if (LE(R(st + 0x68), L16B_CVT_S_W(s)) || (B(self + 0xA) & 1)) WH(self + 0x56, 0);
    } else {
        (void)e0014BB10(o, self, st);
        if (R(st + 0x38) || (B(self + 0xA) & 1)) {
            WB(self + 5, 1);
            WB(self + 6, 0);
        }
    }
    return 0;
}

/* 00147B50: VA = self+0xB0, VB = st[0..3], both second words zeroed. */
static void flat_pair(L16B *o, uint32_t self, uint32_t st)
{
    W(VA, R(self + 0xB0));
    W(VA + 4, R(self + 0xB4));
    W(VA + 8, R(self + 0xB8));
    W(VA + 0xC, R(self + 0xBC));
    W(VB, R(st));
    W(VB + 4, R(st + 4));
    W(VB + 8, R(st + 8));
    W(VB + 0xC, R(st + 0xC));
    W(VB + 4, 0);
    W(VA + 4, 0);
}

/* +5 == 1. Case 0 sets up, case 1 calls 0014B7B0 and acts on its result,
 * case 2 waits for +0xC4 to reach st+0x44. Every case ends in 001B12B0 toward
 * st+0x44 at its own rate. Frame 0x40. */
static uint32_t e00147B50(L16B *o, uint32_t self, uint32_t st, uint32_t sp)
{
    uint32_t rate = 0x3D32B8C3, state = B(self + 6), a = 0, b = 0, v;
    int32_t r = 0;
    if (state == 2) {
        a = R(self + 0xC4);
        b = R(st + 0x44);
        rate = 0x3DA0D97C;
        if (EQ(a, b)) WB(self + 6, 1);
    } else if (state == 1) {
        W(st + 0x58, R(self + 0xC4));
        r = (int32_t)e0014B7B0(o, self, st, sp - 0x40);
        if (r == 2) {
        } else if (r) {
            v = B(self + 6);
            WB(self + 6, v + 1);
            C(001B1470, ADD(PI, R(st + 0x58)), &a);
            C(001B1470, SUB(R(st + 0x44), a), &a);
            C(0011DF78, a, &a);
            {
                uint64_t wide = 0;
                C(00128350, a, &wide);
                C(001000E0, wide, 0x3FD921FB60000000ull, &r);
            }
            if (r) {
                int32_t n = SB(st + 0x84) + 1;
                WB(st + 0x84, (uint32_t)n);
                if ((int8_t)n >= 3) {
                    WB(st + 0x84, 0);
                    C(00122BB8, &r);
                    a = L16B_CVT_S_W(((uint32_t)r >> 12) & 0xFF);
                    C(001B1470, MUL(TAU, DIV(a, F(255.0f))), &a);
                    W(st + 0x44, a);
                }
            }
        } else {
            WB(st + 0x84, 0);
            (void)e0014BB10(o, self, st);
            if (R(st + 0x38) || (B(self + 0xA) & 1)) {
                rate = 0x3DB2B8C3;
                C(001B13F0, TARGET, self + 0xB0, F(20.0f), &r);
                if (r) {
                    C(001B1560, self, TARGET, 0x3FC6D3F2, &r);
                    if (r) {
                        r = (int32_t)e0014C130(o, self);
                        WB(self + 5, r && LT(R(0x008104D8), F(100.0f)) ? 7 : 8);
                        WB(self + 6, 0);
                        goto ease;
                    }
                } else {
                    a = R(TARGET);
                    b = R(TARGET + 8);
                    C(001B1240, self + 0xB0, a, b, &a);
                    W(st + 0x44, a);
                }
                r = (int32_t)e0014BC30(o, self, st);
                if (!r) {
                    C(001B1560, self, TARGET, 0x3DB2B8C3, &r);
                    if (r) {
                        C(001B13F0, TARGET, self + 0xB0, F(40.0f), &r);
                        if (!r) {
                            W(VA, 0);
                            W(VA + 4, 0);
                            W(VA + 8, F(50.0f));
                            W(VA + 0xC, ONE);
                            C(001B2B10, self, VA, VA);
                            C(001028B8, VA, self + 0xB0, VA);
                            C(001B3250, self, VA, F(15.0f), &r);
                            if (!r) {
                                WB(self + 5, 2);
                                WB(self + 6, 0);
                            }
                        }
                    }
                }
            } else {
                v = R(st + 0x28);
                if (v) {
                    W(st + 0x28, v - 1);
                } else {
                    WB(self + 5, 0);
                    WB(self + 6, 0);
                }
                if (SB(st + 0x82)) {
                    r = SH(0x70003B8A);
                    if (!((R(0x70003B68) + (uint32_t)r) & 0x3F)) {
                        flat_pair(o, self, st);
                        C(001B13F0, VA, VB, F(25.0f), &r);
                        if (r) WB(st + 0x82, 0);
                        a = R(st);
                        b = R(st + 8);
                        C(001B1240, self + 0xB0, a, b, &a);
                        W(st + 0x44, a);
                    }
                } else {
                    r = SH(0x70003B8A);
                    if (!((R(0x70003B68) + (uint32_t)r) & 0x3F)) {
                        flat_pair(o, self, st);
                        C(001B13F0, VA, VB, F(50.0f), &r);
                        if (!r) WB(st + 0x82, 1);
                    }
                }
            }
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        WB(st + 0x84, 0);
        W(st + 0x4C, 0x3E99999A);
        C(00122BB8, &r);
        W(st + 0x28, (((uint32_t)r >> 3) & 0x7F) + 0xB4);
        C(00122BB8, &r);
        a = L16B_CVT_S_W(((uint32_t)r >> 16) & 0xFF);
        C(001B1470, DIV(MUL(TAU, a), F(255.0f)), &a);
        W(st + 0x44, a);
        if (((uint32_t)SH(self + 0x2C) & 0xFFFF7FFFu) != 2) C(001C67E0, self, 2, F(5.0f), 0);
    }
ease:
    a = R(st + 0x44);
    b = R(self + 0xC4);
    C(001B12B0, a, b, rate, &a);
    W(self + 0xC4, a);
    return 0;
}

/* +5 == 2: clips 3, 4, 5. Case 2 clamps st+0x4C at 0.6 and advances on any of
 * its probes; case 3 floors st+0x4C at 0. */
static uint32_t e00148130(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6), a, b;
    int32_t r = 0;
    if (state == 3) {
        if (LT(R(st + 0x4C), 0)) {
            W(st + 0x50, 0);
            W(st + 0x4C, 0);
        }
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
            W(st + 0x50, 0);
            W(st + 0x4C, 0);
            C(001C67E0, self, 0, F(5.0f), 0);
        }
    } else if (state == 2) {
        if (!LE(R(st + 0x4C), 0x3F19999A)) {
            W(st + 0x4C, 0x3F19999A);
            W(st + 0x50, 0);
        }
        W(VA, 0);
        W(VA + 4, F(3.0f));
        W(VA + 8, F(30.0f));
        W(VA + 0xC, ONE);
        C(001B2B10, self, VA, VA);
        C(001028B8, VA, VA, self + 0xB0);
        C(001B3250, self, VA, F(15.0f), &r);
        if (!r && !(SB(st + 0x81) & 1)) {
            C(001B1560, self, TARGET, 0x3FB2B8C3, &r);
            if (r) {
                C(001B13F0, TARGET, self + 0xB0, F(30.0f), &r);
                if (!r) return 0;
            }
        }
        WB(self + 6, B(self + 6) + 1);
        W(st + 0x50, 0xBC23D70A);
        C(001C67E0, self, 5, ONE, 0);
    } else if (state == 1) {
        a = R(TARGET);
        b = R(TARGET + 8);
        C(001B1240, self + 0xB0, a, b, &a);
        W(st + 0x44, a);
        b = R(self + 0xC4);
        a = R(st + 0x44);
        C(001B12B0, a, b, 0x3D0EFA35, &a);
        W(self + 0xC4, a);
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, B(self + 6) + 1);
            W(st + 0x50, 0x3CA3D70A);
            C(001C67E0, self, 4, ONE, 0);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, self, 3, ONE, 0);
    }
    return 0;
}

/* 00148400 and 00148520: effect 0x80000016 at +0xB0, height st+0x6C. */
static uint32_t spawn_16(L16B *o, uint32_t self, uint32_t st)
{
    C(00102948, VA, self + 0xB0);
    W(VA + 4, R(st + 0x6C));
    C(001EFD90, (int32_t)0x80000016, VA, self + 0xC0);
    C(001E8B90, VA, F(5.0f));
    return 0;
}

/* +5 == 3: clip 6. Case 0 falls through into case 1. */
static uint32_t e00148400(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6);
    if (state == 0) {
        WB(self + 6, state + 1);
        WB(st + 0x8D, SB(st + 0x8D) & 1);
        W(st + 0x4C, 0);
        C(001C67E0, self, 6, ONE, 0);
    } else if (state != 1) {
        return 0;
    }
    if (R(st + 0x20) & 0x1000) {
        WB(self + 5, 4);
        WB(self + 6, 0);
        W(st + 0x4C, 0x3ECCCCCD);
        W(st + 0x48, 0x40066666);
        C(001FBD50, self, 0x7E6, 0, RANGE);
        if (SB(st + 0x8D) == 1) (void)spawn_16(o, self, st);
    }
    return 0;
}

/* +5 == 4: clips 7, 8. Case 0 falls through into case 1. */
static uint32_t e00148520(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6);
    int32_t v;
    if (state == 2) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
        }
        return 0;
    }
    if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, ONE);
        WB(st + 0x88, 0);
        WB(st + 0x83, 0);
        W(st + 0x50, 0);
        WB(st + 0x8D, SB(st + 0x8D) & 1);
        C(001C67E0, self, 7, ONE, 0);
        if (SB(st + 0x86)) {
            WB(st + 0x86, 0);
            C(0021C040, PLAYER, self);
            if (B(FLAG_6BD) == 1) WB(FLAG_6BD, 0);
        }
    } else if (state != 1) {
        return 0;
    }
    v = SB(st + 0x8D);
    if (v == 1) {
        WB(st + 0x8D, (uint32_t)v | 2);
        (void)spawn_16(o, self, st);
    }
    if (LE(R(st + 0x48), 0) && (SB(st + 0x81) & 2)) {
        WB(self + 6, B(self + 6) + 1);
        W(st + 0x4C, 0);
        W(st + 0x48, 0);
        C(001FBD50, self, 0x7E6, 0, RANGE);
        C(001C67E0, self, 8, ONE, 0);
    }
    return 0;
}

/* 001486E0 and 00148B40: copies the matrix at (word self+bone) + 0x90 into
 * MAT, transforms (x, 0, 0, 1) into its translation row, sets VB to 0.8. */
static uint32_t matrix_setup(L16B *o, uint32_t self, uint32_t bone, uint32_t x)
{
    uint32_t a, b, c, d;
    a = R(self + bone);
    C(00102958, MAT, a + 0x90);
    W(VA, x);
    W(VA + 4, 0);
    W(VA + 8, 0);
    W(VA + 0xC, ONE);
    C(001026A0, VA, MAT, VA);
    a = R(VA);
    b = R(VA + 4);
    c = R(VA + 8);
    d = R(VA + 0xC);
    W(MAT_T, a);
    W(MAT_T + 4, b);
    W(MAT_T + 8, c);
    W(MAT_T + 0xC, d);
    W(VB, 0x3F4CCCCD);
    W(VB + 4, 0x3F4CCCCD);
    W(VB + 8, 0x3F4CCCCD);
    W(VB + 0xC, ONE);
    return 0;
}

/* +5 == 5: clips 0xD, 0xE, 0xF; 001B3C30 on the st+0x28 and st+0x2C timers. */
static uint32_t e001486E0(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6), v;
    int32_t r = 0;
    if (state == 3) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
            W(st + 0x3C, 0);
            W(st + 0x38, 0);
            WB(st + 0x88, 0);
        }
    } else if (state == 2) {
        if (R(st + 0x20) & 0x1000) {
            v = R(st + 0x30);
            if (v) {
                W(st + 0x30, v - 1);
                C(001C67E0, self, 0xE, ONE, 0);
            } else {
                WB(self + 6, state + 1);
                C(001C67E0, self, 0xF, ONE, 0);
            }
            return 0;
        }
        v = R(st + 0x28);
        if (v) {
            W(st + 0x28, v - 1);
        } else {
            W(st + 0x28, 0xA);
            (void)matrix_setup(o, self, 0x17C, F(11.0f));
            C(001B3C30, self, MAT, VB, F(200.0f), B(FLAG_70A) ? F(20.0f) : F(15.0f));
            C(001FBD50, self, 0x7E5, 0, RANGE);
        }
        v = R(st + 0x2C);
        if (v) {
            W(st + 0x2C, v - 1);
        } else {
            W(st + 0x2C, 0xA);
            (void)matrix_setup(o, self, 0x180, F(8.0f));
            C(001B3C30, self, MAT, VB, F(200.0f), B(FLAG_70A) ? F(20.0f) : F(15.0f));
            C(001FBD50, self, 0x7E5, 0, RANGE);
        }
    } else if (state == 1) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, state + 1);
            C(00122BB8, &r);
            W(st + 0x28, ((uint32_t)r >> 16) & 7);
            C(00122BB8, &r);
            W(st + 0x2C, ((uint32_t)r >> 11) & 7);
            C(00122BB8, &r);
            W(st + 0x30, (((uint32_t)r >> 13) & 1) + 1);
            C(001C67E0, self, 0xE, ONE, 0);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        WB(st + 0x88, 1);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, self, 0xD, F(5.0f), 0);
    }
    return 0;
}

/* 00148B40: 001C81C0 with the word of `table` (rows of nine words, row 1 for
 * +0xD bit 0x80), the frame +0x3C as an integer, into dst. */
static uint32_t pose_load(L16B *o, uint32_t e, uint32_t table, uint32_t dst)
{
    uint32_t row, clip, set;
    int32_t frame = 0;
    C(001281C0, R(e + 0x3C), &frame);
    row = (B(e + 0xD) & 0x80) >> 7;
    clip = R(table + row * 9 * 4);
    set = R(e + 0x40);
    C(001C81C0, set, (int32_t)clip, frame, dst);
    return 0;
}

/* 00148B40: 001C9D50 over the +0xC records (0x40 bytes each), the count read
 * again every pass. */
static uint32_t pose_blend(L16B *o, uint32_t e, uint32_t dst, uint32_t a, uint32_t b, uint32_t w)
{
    uint32_t i;
    for (i = 0; i < B(e + 0xC); ++i)
        C(001C9D50, dst + i * 0x40, a + i * 0x40, b + i * 0x40, w);
    return 0;
}

/* +5 == 6 (the decomp's name for this address is anim_matrix_multi; its only
 * caller is 00147700). Case 2 steers, blends the pose buffers and runs the
 * 001B3C30 timers. `turn` nonzero ends in 001B1240 and 001B12B0. */
static uint32_t e00148B40(L16B *o, uint32_t e, uint32_t st)
{
    uint32_t turn = 0, state = B(e + 6), a = 0, b, c, wy = 0, wp = 0, v;
    int32_t r = 0;
    if (state == 3) {
        if (R(st + 0x20) & 0x1000) {
            WB(e + 5, 0);
            WB(e + 6, 0);
            W(st + 0x3C, 0);
            W(st + 0x38, 0);
            WB(st + 0x88, 0);
        }
    } else if (state == 2) {
        turn = 0x3B64C389;
        if (EQ(R(st + 0x64), 0)) {
            a = R(TARGET);
            b = R(e + 0xB0);
            c = R(TARGET + 8);
            W(OUT0, SUB(a, b));
            b = R(e + 0xB8);
            a = R(OUT0);
            c = SUB(c, b);
            W(OUT1, c);
            C(0011E620, a, c, &a);
            C(001B1470, SUB(a, R(e + 0xC4)), &a);
            W(st + 0x60, a);
            a = R(st + 0x58);
            b = R(st + 0x60);
            W(st + 0x64, LE(a, b) ? 0x3C39DEDEu : 0xBC39DEDEu);
        }
        b = R(st + 0x64);
        a = R(st + 0x58);
        a = ADD(a, b);
        W(st + 0x58, a);
        C(0011DF78, a, &a);
        if (!LT(a, PI4)) {
            W(st + 0x64, 0);
            C(001B1560, e, TARGET, PI4, &r);
            if (!r) W(st + 0x30, 0);
        }
        a = R(TARGET);
        b = R(e + 0xB0);
        c = R(TARGET + 8);
        W(OUT0, SUB(a, b));
        b = R(e + 0xB8);
        a = R(OUT0);
        c = SUB(c, b);
        W(OUT1, c);
        C(0011E748, L16B_MADD(L16B_MULA(a, a), c, c), &a);
        W(OUT0, a);
        W(OUT1, R(e + 0xB4));
        v = B(e + 0xD) & 0x80 ? F(10.0f) : F(30.0f);
        W(OUT1, ADD(R(OUT1), v));
        a = R(OUT1);
        b = R(0x00810364);
        c = R(OUT0);
        a = SUB(a, b);
        W(OUT1, a);
        C(0011E620, a, c, &a);
        C(001B1470, a, &a);
        W(st + 0x5C, a);
        if (LT(a, 0xBF490FDB)) W(st + 0x5C, 0xBF490FDB);
        else if (!LE(a, PI4)) W(st + 0x5C, PI4);
        b = R(st + 0x54);
        a = R(st + 0x5C);
        C(001B12B0, a, b, 0x3C8EFA35, &a);
        W(st + 0x54, a);
        C(0011DF78, a, &wy);
        if (!LE(wy, ONE)) wy = ONE;
        C(0011DF78, R(st + 0x58), &wp);
        if (!LE(wp, ONE)) wp = ONE;
        if (LT(R(st + 0x54), 0)) {
            (void)pose_load(o, e, 0x00245B50, POSE_A);
            (void)pose_load(o, e, 0x00245B5C, POSE_B);
            (void)pose_blend(o, e, POSE_C, POSE_A, POSE_B, wy);
            if (LT(R(st + 0x58), 0)) {
                (void)pose_load(o, e, 0x00245B4C, POSE_A);
                (void)pose_load(o, e, 0x00245B58, POSE_B);
            } else {
                (void)pose_load(o, e, 0x00245B54, POSE_A);
                (void)pose_load(o, e, 0x00245B60, POSE_B);
            }
        } else {
            (void)pose_load(o, e, 0x00245B50, POSE_A);
            (void)pose_load(o, e, 0x00245B44, POSE_B);
            (void)pose_blend(o, e, POSE_C, POSE_A, POSE_B, wy);
            if (LT(R(st + 0x58), 0)) {
                (void)pose_load(o, e, 0x00245B4C, POSE_A);
                (void)pose_load(o, e, 0x00245B40, POSE_B);
            } else {
                (void)pose_load(o, e, 0x00245B54, POSE_A);
                (void)pose_load(o, e, 0x00245B48, POSE_B);
            }
        }
        (void)pose_blend(o, e, POSE_D, POSE_A, POSE_B, wy);
        (void)pose_blend(o, e, POSE_A, POSE_C, POSE_D, wp);
        a = ADD(R(e + 0x3C), ONE);
        W(e + 0x3C, a);
        if (!LT(a, F(25.0f))) W(e + 0x3C, 0);
        C(001C82A0, e, POSE_A);
        v = R(st + 0x28);
        if (v) {
            W(st + 0x28, v - 1);
        } else {
            W(st + 0x28, B(e + 0xD) & 0x80 ? 10 : 7);
            (void)matrix_setup(o, e, 0x17C, F(11.0f));
            if (B(e + 0xD) & 0x80) {
                C(001B3C30, e, MAT, VB, F(200.0f), B(FLAG_70A) ? F(20.0f) : F(15.0f));
            } else {
                C(001B3C30, e, MAT, VB, F(200.0f), B(FLAG_70A) ? F(15.0f) : F(10.0f));
            }
            C(001FBD50, e, 0x7E5, 0, RANGE);
        }
        if (B(e + 0xD) & 0x80) {
            v = R(st + 0x2C);
            if (v) {
                W(st + 0x2C, v - 1);
            } else {
                W(st + 0x2C, 10);
                (void)matrix_setup(o, e, 0x180, F(8.0f));
                C(001B3C30, e, MAT, VB, F(200.0f), B(FLAG_70A) ? F(20.0f) : F(15.0f));
                C(001FBD50, e, 0x7E5, 0, RANGE);
            }
        }
        if (LE(R(st + 0x68), F(20.0f))) {
            W(st + 0x30, 0);
            WB(st + 0x83, 0);
            WB(e + 0x5D, 1);
            v = 8;
            if (B(FLAG_4E4) != 1) {
                W(VA, 0);
                W(VA + 4, F(10.0f));
                W(VA + 8, F(21.0f));
                W(VA + 0xC, ONE);
                C(001B2B10, e, VA, VA);
                W(VB, R(e + 0xB0));
                W(VB + 4, R(e + 0xB4));
                W(VB + 8, R(e + 0xB8));
                W(VB + 0xC, R(e + 0xBC));
                W(VB + 4, ADD(R(VB + 4), F(10.0f)));
                C(001028B8, VA, VB, VA);
                C(0019A570, VB, VA, 6, 0, &r);
                if (!r) v = 7;
            }
            WB(e + 5, v);
            WB(e + 6, 0);
        } else {
            v = R(st + 0x30);
            if (v) {
                W(st + 0x30, v - 1);
            } else {
                WB(e + 6, B(e + 6) + 1);
                WB(st + 0x83, 0);
                C(001C67E0, e, B(e + 0xD) & 0x80 ? 0xC : 0xA, F(5.0f), 0);
            }
        }
    } else if (state == 1) {
        turn = 0x3D0EFA35;
        if (R(st + 0x20) & 0x1000) {
            WB(e + 6, state + 1);
            W(st + 0x54, 0);
            W(st + 0x58, 0);
            W(st + 0x64, 0);
            C(00122BB8, &r);
            W(st + 0x28, ((uint32_t)r >> 5) & 7);
            C(00122BB8, &r);
            W(st + 0x2C, ((uint32_t)r >> 4) & 7);
            W(st + 0x30, 300);
            WB(st + 0x83, 1);
        }
    } else if (state == 0) {
        WB(e + 6, state + 1);
        W(e + 0x3C, 0);
        WB(st + 0x83, 0);
        WB(st + 0x88, 1);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, e, B(e + 0xD) & 0x80 ? 0xB : 9, F(5.0f), turn);
    }
    if (!EQ(turn, 0)) {
        a = R(TARGET);
        b = R(TARGET + 8);
        C(001B1240, e + 0xB0, a, b, &a);
        W(st + 0x44, a);
        C(001B12B0, a, R(e + 0xC4), turn, &a);
        W(e + 0xC4, a);
    }
    return 0;
}

/* +5 == 7. Every run copies the 0x40 bytes at 0x245B90 into the frame
 * (sp - 0x40), used or not, as four quadwords loaded and then stored. Case 2
 * tests them with 001B1EA0 at the end of the clip; case 3 acts at frame 14.
 * Frame 0x90. */
static uint32_t e00149B50(L16B *o, uint32_t self, uint32_t st, uint32_t sp)
{
    const uint32_t probe = sp - 0x40;
    uint8_t template_[4][16];
    uint32_t state, a, b, c, d, i;
    int32_t r = 0;
    for (i = 0; i < 4; ++i) l16b_q(o, 0x00245B90 + i * 0x10, template_[i]);
    for (i = 0; i < 4; ++i) l16b_wq(o, probe + i * 0x10, template_[i]);
    state = B(self + 6);
    if (state == 4) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
            WB(st + 0x88, 0);
            W(st + 0x40, ONE);
        }
    } else if (state == 3) {
        if (EQ(F(14.0f), R(self + 0x3C))) {
            W(VA, 0x3FC00000);
            W(VA + 4, F(15.0f));
            W(VA + 8, F(15.0f));
            W(VA + 0xC, ONE);
            C(001B2B10, self, VA, VA);
            C(001028B8, VA, self + 0xB0, VA);
            C(001029C0, MAT);
            C(00102BB0, MAT, MAT, R(self + 0xC4));
            a = R(VA);
            b = R(VA + 4);
            c = R(VA + 8);
            d = R(VA + 0xC);
            W(MAT_T, a);
            W(MAT_T + 4, b);
            W(MAT_T + 8, c);
            W(MAT_T + 0xC, d);
            C(001EFEB0, (int32_t)0x8000002A, MAT);
            if (B(self + 0xD) & 0x80) {
                if (B(FLAG_70A)) {
                    W(0x008104D4, F(25.0f));
                    W(0x008104DC, F(45.0f));
                } else {
                    W(0x008104D4, F(20.0f));
                    W(0x008104DC, F(40.0f));
                }
            } else {
                W(0x008104D4, B(FLAG_70A) ? F(20.0f) : F(18.0f));
                W(0x008104DC, F(35.0f));
            }
            WB(PLAYER, B(PLAYER) | 2);
            WB(st + 0x86, 0);
            C(0021C040, PLAYER, self);
            if (B(FLAG_6BD) == 1) WB(FLAG_6BD, 0);
        }
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, B(self + 6) + 1);
            C(001C67E0, self, 0x13, ONE, 0);
        }
    } else if (state == 2) {
        if (EQ(F(7.0f), R(self + 0x3C))) C(001FBD50, self, 0x7E2, 0, RANGE);
        a = R(TARGET);
        b = R(TARGET + 8);
        C(001B1240, self + 0xB0, a, b, &a);
        W(st + 0x44, a);
        b = R(self + 0xC4);
        a = R(st + 0x44);
        C(001B12B0, a, b, 0x3D8EFA35, &a);
        W(self + 0xC4, a);
        if (R(st + 0x20) & 0x1000) {
            C(0021BED0, PLAYER, &r);
            if (!r && e0014C130(o, self)) {
                b = R(self + 0xB4);
                a = R(0x00810354);
                C(0011DF78, SUB(a, b), &a);
                if (LE(a, F(6.0f))) {
                    for (i = 0; i < 4; ++i) {
                        C(001B2B10, self, probe + i * 0x10, probe + i * 0x10);
                        C(001028B8, probe + i * 0x10, probe + i * 0x10, self + 0xB0);
                    }
                    C(001B1EA0, 0, TARGET, probe, 4, &r);
                    if (r) {
                        WB(self + 6, B(self + 6) + 1);
                        C(0021BF90, PLAYER, self);
                        WB(st + 0x86, 1);
                        C(001B1470, ADD(PI, R(self + 0xC4)), &a);
                        W(0x00810374, a);
                        W(VA, 0xBF000000);
                        W(VA + 4, 0);
                        W(VA + 8, 0x41893333);
                        W(VA + 0xC, ONE);
                        C(001B2B10, self, VA, VA);
                        C(001028B8, VA, VA, self + 0xB0);
                        C(00182F90, PLAYER, VA);
                        C(001C67E0, self, 0x12, ONE, 0);
                        return 0;
                    }
                }
            }
            WB(self + 6, 4);
            C(001C67E0, self, 0x14, ONE, 0);
        }
    } else if (state == 1) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, state + 1);
            W(st + 0x40, ONE);
            C(001C67E0, self, 0x11, 0, 0);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, F(2.0f));
        WB(st + 0x83, 0);
        WB(st + 0x88, 1);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, self, 0x10, F(5.0f), 0);
    }
    return 0;
}

/* +5 == 8: clips 0x10, 0x11, 0x14; st+0x28 allows one 001B55E0 per run. */
static uint32_t e0014A0D0(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6), a, b;
    int32_t r = 0;
    if (state == 3) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
            WB(st + 0x88, 0);
            W(st + 0x40, ONE);
        }
    } else if (state == 2) {
        if (EQ(F(7.0f), R(self + 0x3C))) C(001FBD50, self, 0x7E2, 0, RANGE);
        a = R(TARGET);
        b = R(TARGET + 8);
        C(001B1240, self + 0xB0, a, b, &a);
        W(st + 0x44, a);
        b = R(self + 0xC4);
        a = R(st + 0x44);
        C(001B12B0, a, b, 0x3D8EFA35, &a);
        W(self + 0xC4, a);
        if (!R(st + 0x28)) {
            C(0021BE40, PLAYER, self, &r);
            if (!r) {
                C(001A7B80, self, &r);
                if (r) {
                    W(st + 0x28, 1);
                    WB(PLAYER, B(PLAYER) | 2);
                    W(0x008104D4, B(self + 0xD) & 0x80 ? F(30.0f) : F(20.0f));
                    a = R(TARGET);
                    b = R(self + 0xB0);
                    W(0x00810320, SUB(a, b));
                    W(0x00810324, 0);
                    a = R(TARGET + 8);
                    b = R(self + 0xB8);
                    W(0x00810328, SUB(a, b));
                    C(00102760, 0x00810320, 0x00810320);
                    C(001B55E0, self, 0);
                }
            }
        }
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, B(self + 6) + 1);
            C(001C67E0, self, 0x14, 0, 0);
        }
    } else if (state == 1) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, state + 1);
            W(st + 0x40, ONE);
            C(001C67E0, self, 0x11, 0, 0);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x28, 0);
        W(st + 0x40, F(2.0f));
        WB(st + 0x83, 0);
        WB(st + 0x88, 1);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, self, 0x10, F(5.0f), 0);
    }
    return 0;
}

/* +5 == 9: clips 0x32..0x35; ends by setting +0xD bit 0x80 and +0x34. */
static uint32_t e0014A350(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6), v;
    int32_t r = 0;
    if (state == 4) {
        WB(st + 0x85, 0x64);
        if (R(st + 0x20) & 0x1000) {
            WB(self + 5, 0);
            WB(self + 6, 0);
            WB(self + 0xD, B(self + 0xD) | 0x80);
            WH(self + 0x34, B(FLAG_70A) ? 0x190 : 0xFA);
            C(001FBD50, self, 0x7EF, 0, RANGE);
        }
    } else if (state == 3) {
        WB(st + 0x85, 0x64);
        if (R(st + 0x20) & 0x1000) {
            if (EQ(0x3F933333, R(self + 0x60))) {
                WB(self + 6, B(self + 6) + 1);
                C(001C67E0, self, 0x35, 0, 0);
                C(001FBD50, self, 0x7EE, 0, RANGE);
            }
        }
        W(VA, 0x3A83126F);
        W(VA + 4, 0x3A83126F);
        W(VA + 8, 0x3A83126F);
        W(VA + 0xC, 0);
        C(001028B8, self + 0x60, VA, self + 0x60);
        if (!LE(R(self + 0x60), 0x3F933333)) {
            W(self + 0x60, 0x3F933333);
            W(self + 0x64, 0x3F933333);
            W(self + 0x68, 0x3F933333);
            W(self + 0x6C, ONE);
        }
    } else if (state == 2) {
        v = R(st + 0x28);
        if (v) {
            W(st + 0x28, v - 1);
        } else {
            WB(self + 6, state + 1);
            WB(st + 0x85, 0x64);
            C(001EFE00, (int32_t)0x8000002B, self, &r);
            C(001C67E0, self, 0x34, F(5.0f), 0);
            C(001FBD50, self, 0x7ED, 0, RANGE);
        }
    } else if (state == 1) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, state + 1);
            C(00122BB8, &r);
            W(st + 0x28, (((uint32_t)r >> 11) & 0xFF) + 0x3C);
            C(001C67E0, self, 0x33, 0, 0);
            C(001FBD50, self, 0x7EC, 0, RANGE);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, ONE);
        WB(st + 0x80, 0);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001C67E0, self, 0x32, F(5.0f), 0);
    }
    return 0;
}

/* +4 == 2: the handlers by +5, the st+0x86 release (0021C040), then the fixed
 * per-frame chain. */
static uint32_t e0014A620(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 5);
    int32_t r = 0;
    if (state == 2) (void)e0014ACC0(o, self, st);
    else if (state == 1) (void)e0014A9A0(o, self, st);
    else if (state == 0) (void)e0014A730(o, self, st);
    if (SB(st + 0x86)) {
        WB(st + 0x86, 0);
        C(0021C040, PLAYER, self);
        if (B(FLAG_6BD) == 1) WB(FLAG_6BD, 0);
    }
    (void)e0014BE20(o, self, st);
    (void)e0014AE80(o, self, st);
    (void)e0014AFA0(o, self, st);
    C(001C64F0, self, R(st + 0x40), &r);
    W(st + 0x20, (uint32_t)r);
    C(00131ED0, self);
    C(001C68C0, self);
    C(001B17A0, self, &r);
    (void)l16b_method(o, self);
    return 0;
}

/* 0014A730 and 0014A9A0: +5 = 8 when st+0x68 <= 20, else 6. */
static uint32_t reach_state(L16B *o, uint32_t self, uint32_t st)
{
    WB(self + 5, LE(R(st + 0x68), F(20.0f)) ? 8 : 6);
    return 0;
}

/* +4 == 2, +5 == 0. Case 0 falls through into case 1. */
static uint32_t e0014A730(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6);
    int32_t r = 0, rem;
    if (state == 2) {
        if (LE(R(self + 0x3C), F(45.0f))) WB(st + 0x87, 0);
        if (R(st + 0x20) & 0x1000) {
            WB(self + 4, 1);
            WB(self + 6, 0);
            WB(st + 0x87, 0);
            WB(self, 1);
            WH(st + 0x72, 0);
            WH(self + 0x36, 0);
            WB(st + 0x85, 0x3C);
            (void)reach_state(o, self, st);
        }
        return 0;
    }
    if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, ONE);
        WB(st + 0x88, 0);
        WB(st + 0x83, 0);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        if (SB(st + 0x87)) {
            C(001C67E0, self, 0x30, ONE, 0);
        } else if ((uint32_t)SH(self + 0x36) & 0x8000) {
            C(001FBD50, self, 0x7E9, 0, RANGE);
            C(001C67E0, self, 0x2E, ONE, 0);
        } else {
            WB(self, 1);
            WH(self + 0x36, 0);
            WB(st + 0x85, 0);
            C(001FBD50, self, 0x7E7, 0, RANGE);
            C(00122BB8, &r);
            rem = l16b_sra((uint32_t)r, 17) % 3;
            C(001C67E0, self, SH(0x00245BD0 + (uint32_t)rem * 4), ONE, 0);
        }
    } else if (state != 1) {
        return 0;
    }
    if (R(st + 0x20) & 0x1000) {
        if (SB(st + 0x87)) {
            WB(self + 6, B(self + 6) + 1);
            C(001C67E0, self, 0x2D, 0, 0);
            return 0;
        }
        WB(self + 4, 1);
        WB(self + 6, 0);
        WB(self, 1);
        WH(st + 0x72, 0);
        WH(self + 0x36, 0);
        WB(st + 0x85, 0x3C);
        (void)reach_state(o, self, st);
    }
    return 0;
}

/* +4 == 2, +5 == 1. Works on the float at ((word 0x275B40) + 4) + 0x74: adds
 * st+0x44 to it in case 1, then folds it into +0xC4 and zeroes it. */
static uint32_t e0014A9A0(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6), a, b, p;
    int32_t r = 0;
    if (state == 2) {
        if (R(st + 0x20) & 0x1000) {
            WB(self + 4, 1);
            WB(self + 6, 0);
            WB(st + 0x87, 0);
            WB(st + 0x80, 0);
            WB(self, 1);
            WH(st + 0x72, 0);
            WH(self + 0x36, 0);
            WB(st + 0x85, 0x3C);
            (void)reach_state(o, self, st);
        }
    } else if (state == 1) {
        p = R(0x00275B40);
        a = R(st + 0x44);
        p = R(p + 4);
        b = R(p + 0x74);
        C(001B1470, ADD(b, a), &a);
        p = R(0x00275B40);
        p = R(p + 4);
        W(p + 0x74, a);
        if (SB(st + 0x81) & 2) {
            W(st + 0x4C, 0);
            if (R(st + 0x20) & 0x1000) {
                WB(self + 6, B(self + 6) + 1);
                WB(st + 0x87, 1);
                C(001FBD50, self, 0x7E8, 0, RANGE);
                p = R(0x00275B40);
                a = R(self + 0xC4);
                p = R(p + 4);
                b = R(p + 0x74);
                C(001B1470, ADD(a, b), &a);
                W(self + 0xC4, a);
                p = R(0x00275B40);
                p = R(p + 4);
                W(p + 0x74, 0);
                if (SH(self + 0x34)) {
                    C(001C67E0, self, 0x2D, ONE, 0);
                } else {
                    WB(self + 5, 2);
                    WB(self + 6, 0);
                }
            }
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, ONE);
        WB(st + 0x80, 0x80);
        WB(st + 0x88, 0);
        WB(st + 0x83, 0);
        W(st + 0x48, F(1.5f));
        W(st + 0x50, 0);
        C(001FBD50, self, 0x7E7, 0, RANGE);
        C(00122BB8, &r);
        a = L16B_CVT_S_W(((uint32_t)r >> 19) & 0xFF);
        W(st + 0x44, MUL(0x3E0EFA35, DIV(a, F(255.0f))));
        C(00122BB8, &r);
        if (((uint32_t)r >> 12) & 1) W(st + 0x44, MUL(R(st + 0x44), NEG_ONE));
        b = R(self + 0x78);
        a = R(self + 0x70);
        C(0011E620, a, b, &a);
        C(001B1470, SUB(a, R(self + 0xC4)), &a);
        W(OUT0, a);
        C(0011DF78, a, &a);
        if (!LT(a, PI2)) {
            W(st + 0x4C, NEG_ONE);
            a = R(self + 0xC4);
            b = R(OUT0);
            C(001B1470, ADD(PI, ADD(a, b)), &a);
            W(self + 0xC4, a);
            C(001C67E0, self, 0x2A, 0, 0);
        } else {
            W(st + 0x4C, ONE);
            a = R(self + 0xC4);
            b = R(OUT0);
            C(001B1470, ADD(a, b), &a);
            W(self + 0xC4, a);
            C(001C67E0, self, 0x2B, 0, 0);
        }
    }
    return 0;
}

/* +4 == 2, +5 == 2: clip 0x2F or 0x31; a zero 001EFE00 sets +4 = 3. */
static uint32_t e0014ACC0(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t state = B(self + 6);
    int32_t r = 0;
    if (state == 2) return 0;
    if (state == 1) {
        if (((uint32_t)SH(self + 0x2C) & 0xFFFF7FFFu) == 0x31 && EQ(F(54.0f), R(self + 0x3C)))
            C(001FBD50, self, 0x7E8, 0, RANGE);
        if (R(st + 0x20) & 0x1000) {
            WB(self + 6, B(self + 6) + 1);
            WB(st + 0x8A, 0);
            C(001FBD50, self, 0x7EA, 0, RANGE);
            C(001EFE00, (int32_t)0x8000001E, self, &r);
            if (!r) WB(self + 4, 3);
        }
    } else if (state == 0) {
        WB(self + 6, state + 1);
        W(st + 0x40, ONE);
        WB(st + 0x80, 0);
        WB(st + 0x88, 0);
        W(st + 0x50, 0);
        W(st + 0x4C, 0);
        C(001B1190, (int32_t)B(self + 0x9A));
        C(001FBD50, self, 0x7EB, 0, RANGE);
        C(001C67E0, self, SB(st + 0x87) ? 0x2F : 0x31, ONE, 0);
    }
    return 0;
}

/* +4 == 3. */
static uint32_t e0014AE50(L16B *o, uint32_t self)
{
    C(001B1190, (int32_t)B(self + 0x9A));
    C(001AFC10, self);
    return 0;
}

/* Height rate st+0x48 (-0.1 per frame) into +0xB4, then speed st+0x4C along
 * +0xC4 into +0xB0 / +0xB8, unless the 001B1560 test sets +0x52 bit 0. */
static uint32_t e0014AE80(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t a, b, trig = 0;
    int32_t r = 0;
    if (SB(st + 0x80) & 0x7F) return 0;
    a = ADD(R(st + 0x48), 0xBDCCCCCD);
    W(st + 0x48, a);
    W(self + 0xB4, ADD(R(self + 0xB4), a));
    if (SB(st + 0x8C)) return 0;
    if (!(SB(st + 0x80) & 0x80) && LE(R(st + 0x68), F(10.0f))) {
        C(001B1560, self, TARGET, 0x3F91361E, &r);
        if (r) {
            WH(self + 0x52, H(self + 0x52) | 1);
            return 0;
        }
    }
    b = R(st + 0x50);
    a = R(st + 0x4C);
    W(st + 0x4C, ADD(a, b));
    C(0011E2A8, R(self + 0xC4), &trig);
    a = R(st + 0x4C);
    b = R(self + 0xB0);
    W(self + 0xB0, ADD(b, MUL(a, trig)));
    C(0011DE90, R(self + 0xC4), &trig);
    a = R(st + 0x4C);
    b = R(self + 0xB8);
    W(self + 0xB8, ADD(b, MUL(a, trig)));
    return 0;
}

/* The probes behind st+0x81: bit 1 from the 001B32F0 / 001B3390 group, bit 2
 * from 001B3440, bit 4 from 0019AB20; surface 0x5B in the contact record sets
 * st+0x8D bit 0 and st+0x6C. */
static uint32_t e0014AFA0(L16B *o, uint32_t ent, uint32_t st)
{
    uint32_t rate, i, p, y, z;
    int32_t r = 0;
    WB(st + 0x81, 0);
    W(VA, 0);
    W(VA + 4, F(3.0f));
    W(VA + 8, F(3.0f));
    W(VA + 0xC, ONE);
    if (LT(R(st + 0x4C), 0)) W(VA + 8, F(-3.0f));
    C(001B2B10, ent, VA, VA);
    C(001028B8, VA, ent + 0xB0, VA);
    C(001B32F0, ent, VA, PI4, &r);
    if (r) {
        WB(st + 0x81, SB(st + 0x81) | 1);
    } else {
        rate = R(st + 0x48);
        if (!LE(rate, 0)) {
            C(001029C0, MAT);
            C(00102BB0, MAT, MAT, R(ent + 0xC4));
            for (i = 0; i < 4; ++i) {
                C(001026A0, VA, MAT, 0x00245BE0 + (i + 1) * 0x10);
                C(001028B8, VA, ent + 0xB0, VA);
                C(001B32F0, ent, VA, PI4, &r);
                if (r) {
                    WB(st + 0x81, SB(st + 0x81) | 1);
                    break;
                }
            }
        } else if (LT(rate, 0xBDCCCCCD)) {
            W(VA, 0);
            W(VA + 4, 0);
            W(VB, 0);
            W(VB + 4, 0);
            W(VA + 8, F(-3.0f));
            W(VC, F(-3.0f));
            W(VC + 4, 0);
            W(VC + 8, 0);
            W(VA + 0xC, ONE);
            W(VC + 0xC, ONE);
            W(VB + 8, F(3.0f));
            W(VD, F(3.0f));
            W(VB + 0xC, ONE);
            W(VD + 4, 0);
            W(VD + 8, 0);
            W(VD + 0xC, ONE);
            C(001029C0, MAT);
            C(00102BB0, MAT, MAT, R(ent + 0xC4));
            C(001026A0, VA, MAT, VA);
            C(001026A0, VB, MAT, VB);
            C(001026A0, VC, MAT, VC);
            C(001026A0, VD, MAT, VD);
            C(001028B8, VA, ent + 0xB0, VA);
            C(001028B8, VB, ent + 0xB0, VB);
            C(001028B8, VC, ent + 0xB0, VC);
            C(001028B8, VD, ent + 0xB0, VD);
            C(001B3390, ent, VA, VB, PI4, &r);
            if (r) {
                WB(st + 0x81, SB(st + 0x81) | 1);
            } else {
                C(001B3390, ent, VB, VA, PI4, &r);
                if (r) WB(st + 0x81, SB(st + 0x81) | 1);
            }
            if (!SB(st + 0x81)) {
                C(001B3390, ent, VC, VD, PI4, &r);
                if (r) {
                    WB(st + 0x81, SB(st + 0x81) | 1);
                } else {
                    C(001B3390, ent, VD, VC, PI4, &r);
                    if (r) WB(st + 0x81, SB(st + 0x81) | 1);
                }
            }
        }
    }
    if (SB(st + 0x81) & 1) WH(ent + 0x52, H(ent + 0x52) | 1);
    rate = R(st + 0x48);
    if (LT(rate, 0)) {
        C(001029C0, MAT);
        C(00102BB0, MAT, MAT, R(ent + 0xC4));
        W(VB, 0);
        W(VB + 4, F(-15.0f));
        W(VB + 8, 0);
        W(VB + 0xC, ONE);
        for (i = 0; i < 5; ++i) {
            C(001026A0, VA, MAT, 0x00245BE0 + i * 0x10);
            C(001028B8, VA, ent + 0xB0, VA);
            C(001B3440, ent, VA, VB, PI4, &r);
            if (r) {
                W(st + 0x48, 0);
                WB(st + 0x81, SB(st + 0x81) | 2);
                break;
            }
        }
        if (!(SB(st + 0x81) & 2) && B(ent + 4) == 1) {
            p = B(ent + 5);
            if (p != 3 && p != 4) {
                C(001B2F70, ent + 0xB0, VA, &r);
                if (!r) W(VA, SUB(R(ent + 0xB4), F(10.0f)));
                y = R(ent + 0xB4);
                z = R(VA);
                if (!LT(SUB(y, z), F(10.0f)) && LT(R(st + 0x48), F(-0.5f))) {
                    WB(ent + 5, 4);
                    WB(ent + 6, 0);
                }
            }
        }
        C(001031E0, VA, ent + 0xB0);
        W(VA + 4, ADD(R(VA + 4), F(18.0f)));
        WB(st + 0x8D, (uint32_t)SB(st + 0x8D) & ~1u);
        C(0019B6C0, VA, ent + 0xB0, &r);
        if (r) {
            p = R(CONTACT);
            if (B(p + 0x1A) == 0x5B) {
                WB(st + 0x8D, SB(st + 0x8D) | 1);
                W(st + 0x6C, R(0x700031B4));
                if (!SB(st + 0x8E) && !EQ(R(st + 0x4C), 0)) {
                    C(00122BB8, &r);
                    WB(st + 0x8E, (((uint32_t)r >> 9) & 7) + 7);
                    W(VA, R(ent + 0xB0));
                    W(VA + 4, R(ent + 0xB4));
                    W(VA + 8, R(ent + 0xB8));
                    W(VA + 0xC, ONE);
                    W(VA + 4, R(st + 0x6C));
                    C(001EFD90, (int32_t)0x8000001D, VA, ent + 0xC0);
                }
            }
        }
    } else if (!LE(rate, 0)) {
        W(VA, R(ent + 0xB0));
        W(VA + 4, R(ent + 0xB4));
        W(VA + 8, R(ent + 0xB8));
        W(VA + 0xC, R(ent + 0xBC));
        W(VB, 0);
        W(VB + 4, F(15.0f));
        W(VB + 8, 0);
        W(VB + 0xC, ONE);
        W(VA + 4, ADD(R(VA + 4), 0x41A00419));
        C(0019AB20, ent, VA, VB, (int32_t)0x80000007, &r);
        if (r) {
            W(st + 0x48, 0);
            WB(st + 0x81, SB(st + 0x81) | 4);
        }
    }
    return 0;
}

/* 001B2BF0 at 10 units ahead. Returns 1 with a new st+0x44, 0 when bit 1
 * alone came back and the height test passes, 2 after setting +5 = 3. The
 * contact record's words +0x24..+0x2C go into the frame at sp - 0x10.
 * Frame 0x40. */
static uint32_t e0014B7B0(L16B *o, uint32_t self, uint32_t st, uint32_t sp)
{
    const uint32_t normal = sp - 0x10;
    uint32_t flags, p, a, b, c = 0;
    int32_t r = 0;
    W(VA, 0);
    W(VA + 4, F(3.0f));
    W(VA + 8, F(10.0f));
    W(VA + 0xC, ONE);
    C(001B2B10, self, VA, VA);
    C(001028B8, VA, VA, self + 0xB0);
    C(001B2BF0, self, VA, VD, PI4, &r);
    flags = (uint32_t)r;
    if (!(flags & 1)) {
        if (flags & 4) {
            p = R(CONTACT);
            W(normal, R(p + 0x24));
            W(normal + 4, R(p + 0x28));
            W(normal + 8, R(p + 0x2C));
            W(normal + 0xC, ONE);
            C(001B3580, self, normal, &c);
        } else {
            C(001B37D0, self, F(10.0f), F(45.0f), &c);
        }
        W(st + 0x44, c);
        return 1;
    }
    if (flags & 4) {
        p = R(CONTACT);
        a = R(p + 0x24);
        b = R(VD);
        W(normal, a);
        W(normal + 4, R(p + 0x28));
        W(normal + 8, R(p + 0x2C));
        W(normal + 0xC, ONE);
        if (!LT(ADD(F(15.0f), R(self + 0xB4)), b) && B(p + 0x1A) != 0x46) {
            C(00122BB8, &r);
            if (((uint32_t)r >> 14) & 3) {
                W(VA, R(self + 0xB0));
                W(VA + 4, R(self + 0xB4));
                W(VA + 8, R(self + 0xB8));
                W(VA + 0xC, R(self + 0xBC));
                a = R(VA + 4);
                b = R(self + 0xB0);
                a = ADD(a, F(10.0f));
                W(VB, b);
                W(VB + 4, R(self + 0xB4));
                W(VB + 8, R(self + 0xB8));
                W(VB + 0xC, R(self + 0xBC));
                W(VA + 4, a);
                W(VA + 0xC, ONE);
                W(VB + 4, ADD(R(VB + 4), F(40.0f)));
                W(VB + 0xC, ONE);
                C(0019A570, VA, VB, 6, 0, &r);
                if (!r) {
                    W(VA, 0);
                    W(VA + 4, F(15.5f));
                    W(VA + 8, F(10.0f));
                    W(VA + 0xC, ONE);
                    C(001B2B10, self, VA, VA);
                    C(001028B8, VA, VA, self + 0xB0);
                    C(0019AD00, self, VA, 7, &r);
                    if (!r) {
                        WB(self + 5, 3);
                        WB(self + 6, 0);
                        return 2;
                    }
                }
            }
        }
        C(001B3580, self, normal, &c);
        W(st + 0x44, c);
        return 1;
    }
    a = R(self + 0xB4);
    b = R(VD);
    if (LT(SUB(a, F(45.0f)), b)) return 0;
    C(001B37D0, self, F(10.0f), F(45.0f), &c);
    W(st + 0x44, c);
    return 1;
}

/* st+0x38 and its count st+0x3C: set to 0xF0 by +0xA bit 0, or once 001B3F10
 * has passed for 0x275400[D_0081050C & 3] frames; cleared after 120 frames
 * beyond st+0x68 = 50. */
static uint32_t e0014BB10(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t v, a, b;
    int32_t r = 0;
    if (R(st + 0x38)) {
        if (LE(R(st + 0x68), F(50.0f))) {
            W(st + 0x3C, 0);
            return 0;
        }
        v = R(st + 0x3C) + 1;
        W(st + 0x3C, v);
        if (v >= 0x78) {
            W(st + 0x38, 0);
            W(st + 0x3C, 0);
        }
        return 0;
    }
    if (B(self + 0xA) & 1) {
        W(st + 0x38, 0xF0);
        W(st + 0x3C, 0);
        return 0;
    }
    v = R(self + 0x13C);
    b = R(v + 0xB8);
    a = R(v + 0xB0);
    C(0011E620, a, b, &a);
    C(001B1470, SUB(a, PI2), &a);
    C(001B3F10, self, a, F(50.0f), &r);
    if (r) {
        v = R(st + 0x3C) + 1;
        W(st + 0x3C, v);
        a = B(0x0081050C) & 3;
        if (v >= B(0x00275400 + a)) {
            W(st + 0x38, 0xF0);
            W(st + 0x3C, 0);
        }
    } else {
        W(st + 0x3C, 0);
    }
    return 0;
}

/* Returns 1 after setting +5 = 6, or 5 with +0xD bit 0x80 and a roll below 3
 * of 8. */
static uint32_t e0014BC30(L16B *o, uint32_t self, uint32_t st)
{
    uint32_t x, y, z, w, v, flag;
    int32_t r = 0;
    if (!R(st + 0x38)) return 0;
    C(00122BB8, &r);
    if (!(((uint32_t)r >> 19) & 1)) return 0;
    C(0021BE40, PLAYER, self, &r);
    if (r) return 0;
    x = R(0x00810350);
    v = R(self + 0xB0);
    y = R(0x00810354);
    z = R(0x00810358);
    w = R(0x0081035C);
    W(VA, v);
    flag = B(FLAG_4E4);
    W(VA + 4, R(self + 0xB4));
    W(VA + 8, R(self + 0xB8));
    W(VA + 0xC, R(self + 0xBC));
    W(VB, x);
    W(VB + 4, y);
    W(VB + 8, z);
    W(VB + 0xC, w);
    x = R(VA + 4);
    y = R(VB + 4);
    x = ADD(x, F(15.0f));
    y = ADD(y, F(15.0f));
    W(VA + 4, x);
    W(VB + 4, y);
    if (flag != 1) {
        C(001B13F0, VB, VA, F(30.0f), &r);
        if (r) return 0;
    }
    C(001B13F0, VB, VA, F(100.0f), &r);
    if (!r) return 0;
    C(001B1560, self, VB, 0x3EC90FDB, &r);
    if (!r) return 0;
    WB(self + 5, 6);
    WB(self + 6, 0);
    if (B(self + 0xD) & 0x80) {
        C(00122BB8, &r);
        if ((((uint32_t)r >> 18) & 7) < 3) WB(self + 5, 5);
    }
    return 1;
}

/* The pending word +0x36 against +0x34: low 12 bits subtracted (times five
 * with bit 0x8000), 0x4000 runs 001EFE00, 0x2000 changes +4. Returns 1 when +4
 * went to 2. The final load of +0x36 reads back the zero just stored, so
 * st+0x85 = 0x1E is unreachable; the test keeps the load and the branch. */
static uint32_t e0014BE20(L16B *o, uint32_t a, uint32_t b)
{
    int32_t m, delta, t, r = 0;
    if (!SH(a + 0x34) || !SH(a + 0x36)) return 0;
    if (SB(b + 0x85) || SB(b + 0x86)) {
        WH(a + 0x36, 0);
        return 0;
    }
    WB(a, 2);
    W(b + 0x38, 0xF0);
    WB(b + 0x85, 0xFF);
    m = SH(a + 0x36);
    delta = m & 0xFFF;
    if ((uint32_t)m & 0x8000) delta = (int16_t)(delta * 5);
    if (!SB(b + 0x89) && ((uint32_t)m & 0x4000)) {
        WB(b + 0x89, 0x3C);
        C(001EFE00, (int32_t)0x80000027, a, &r);
    }
    if (SH(b + 0x70)) {
        t = SH(a + 0x36);
        r = SH(b + 0x72);
        WH(b + 0x72, (uint32_t)r + ((uint32_t)t & 0xFFF));
    } else {
        WH(b + 0x72, (uint32_t)SH(a + 0x36) & 0xFFF);
    }
    WH(b + 0x70, 0x19);
    t = SH(a + 0x34);
    if (delta >= t) {
        WH(a + 0x34, 0);
        WB(a + 4, 2);
        WB(a + 5, 2);
        WB(a + 6, 0);
        C(001B4CF0, a);
        if (SB(b + 0x87)) return 1;
        if (!(SB(b + 0x81) & 2) || (SH(a + 0x36) & 0x2000)) WB(a + 5, 1);
        return 1;
    }
    WH(a + 0x34, (uint32_t)t - (uint32_t)delta);
    m = SH(a + 0x36);
    if ((uint32_t)m & 0x2000) {
        WB(a + 4, 2);
        WB(a + 6, 0);
        WB(a + 5, SB(b + 0x87) ? 0 : 1);
        return 1;
    }
    if (B(a + 4) != 1) {
        WB(a, 1);
        WH(a + 0x36, 0);
        return 0;
    }
    if (SH(b + 0x72) >= 0x19 || ((uint32_t)m & 0x8000)) {
        WB(a + 4, 2);
        WB(a + 6, 0);
        WH(b + 0x72, 0);
        WB(a + 5, (SB(b + 0x81) & 2) ? 0 : 1);
        return 1;
    }
    WB(a, 1);
    WB(b + 0x8C, 2);
    WH(a + 0x36, 0);
    WB(b + 0x85, (SH(a + 0x36) & 0x5000) ? 0x1E : 0);
    return 0;
}

/* Sets +5 = 9 when all its tests pass. */
static uint32_t e0014C0B0(L16B *o, uint32_t self, uint32_t st)
{
    if (B(self + 0xD) & 0x80) return 0;
    if (SH(st + 0x74)) return 0;
    if (!R(st + 0x38)) return 0;
    if (!SH(self + 0x54)) return 0;
    if (!(SB(st + 0x81) & 2)) return 0;
    if (B(self + 4) != 1) return 0;
    if (B(self + 5) < 3) {
        WB(self + 5, 9);
        WB(self + 6, 0);
    }
    return 0;
}

/* The three rows of 0x245C30 through 0019A440; 1 when none returns nonzero. */
static uint32_t e0014C130(L16B *o, uint32_t self)
{
    uint32_t i;
    int32_t r = 0;
    if (B(self + 0xD) & 1) return 0;
    for (i = 0; i < 3; ++i) {
        C(00102948, VA, 0x00245C30 + i * 0x10);
        C(001B2B10, self, VA, VA);
        C(00102948, VB, self + 0xB0);
        C(001028B8, VA, VB, VA);
        W(VB + 4, R(VA + 4));
        C(0019A440, VB, VA, 6, &r);
        if (r) return 0;
    }
    return 1;
}

#define ENTRY2(addr) \
int em_level16_beast_##addr(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, EmLevel16BeastFault *fault) \
{ \
    L16B o; \
    if (l16b_begin(&o, h, fault)) return -1; \
    (void)e##addr(&o, a0, a1); \
    return l16b_end(&o); \
}
#define ENTRY_SP(addr) \
int em_level16_beast_##addr(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16BeastFault *fault) \
{ \
    L16B o; \
    if (l16b_begin(&o, h, fault)) return -1; \
    (void)e##addr(&o, a0, a1, sp); \
    return l16b_end(&o); \
}

int em_level16_beast_00147390(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t sp, EmLevel16BeastFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e00147390(&o, a0, sp);
    return l16b_end(&o);
}
ENTRY2(001475A0)
ENTRY_SP(00147700)
ENTRY2(00147960)
ENTRY_SP(00147B50)
ENTRY2(00148130)
ENTRY2(00148400)
ENTRY2(00148520)
ENTRY2(001486E0)
ENTRY2(00148B40)
ENTRY_SP(00149B50)
ENTRY2(0014A0D0)
ENTRY2(0014A350)
ENTRY2(0014A620)
ENTRY2(0014A730)
ENTRY2(0014A9A0)
ENTRY2(0014ACC0)

int em_level16_beast_0014AE50(const EmLevel16BeastHooks *h, uint32_t a0, EmLevel16BeastFault *fault)
{
    L16B o;
    if (l16b_begin(&o, h, fault)) return -1;
    (void)e0014AE50(&o, a0);
    return l16b_end(&o);
}
ENTRY2(0014AE80)
ENTRY2(0014AFA0)

int em_level16_beast_0014B7B0(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, int32_t *result, EmLevel16BeastFault *fault)
{
    L16B o;
    uint32_t r;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    r = e0014B7B0(&o, a0, a1, sp);
    if (l16b_end(&o)) return -1;
    *result = (int32_t)r;
    return 0;
}
ENTRY2(0014BB10)

int em_level16_beast_0014BC30(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel16BeastFault *fault)
{
    L16B o;
    uint32_t r;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    r = e0014BC30(&o, a0, a1);
    if (l16b_end(&o)) return -1;
    *result = (int32_t)r;
    return 0;
}

int em_level16_beast_0014BE20(const EmLevel16BeastHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel16BeastFault *fault)
{
    L16B o;
    uint32_t r;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    r = e0014BE20(&o, a0, a1);
    if (l16b_end(&o)) return -1;
    *result = (int32_t)r;
    return 0;
}
ENTRY2(0014C0B0)

int em_level16_beast_0014C130(const EmLevel16BeastHooks *h, uint32_t a0, int32_t *result, EmLevel16BeastFault *fault)
{
    L16B o;
    uint32_t r;
    if (!result || l16b_begin(&o, h, fault)) return -1;
    r = e0014C130(&o, a0);
    if (l16b_end(&o)) return -1;
    *result = (int32_t)r;
    return 0;
}
