/* AREA03 gates and lift controls, from the matching decomp C. UNWIRED. */
#include "em_area03_arrival_internal.h"
#define B(a) a03a_u8(o, (a))
#define R(a) a03a_u32(o, (a))
#define W(a,v) a03a_w32(o, (a), (v))
#define WB(a,v) a03a_w8(o, (a), (v))
#define WH(a,v) a03a_w16(o, (a), (v))
#define F(v) a03a_bits(v)
#define C(fn,...) do { if (a03a_c_##fn(o, __VA_ARGS__)) return; } while (0)

static uint32_t bone(A03A *o, uint32_t offset)
{
    return R(R(0x275B40) + offset);
}
static void gate(A03A *o, uint32_t self, int second)
{
    int32_t r = 0;
    uint32_t phase, work, records;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r); if (r) break;
        if (!second) { W(self + 0x80, F(1.5f)); W(self + 0x84, F(1.5f)); W(self + 0x88, F(1.5f)); }
        WB(self, 1);
        if ((R(0x275CA0) != 0) != second) {
            W(bone(o, 4) + 0x7C, F(-5.0f)); W(bone(o, 8) + 0x7C, F(5.0f)); W(self + 0x1F0, 0);
        } else W(self + 0x1F0, 1);
        C(001C6380, self); break;
    case 1:
        switch (B(self + 5)) {
        case 0:
            if ((R(0x275CA0) == 0) != second) C(001B17A0, self, &r);
            work = R(self + 0x1F0);
            if (work && ((R(0x275CA0) != 0) != second)) {
                WB(self + 5, 1); W(self + 0x1F4, 0);
            } else if (!work && ((R(0x275CA0) == 0) != second)) {
                WB(self + 5, 1); W(self + 0x1F4, F(60.0f));
            }
            break;
        case 1:
            if (R(self + 0x1F0)) {
                phase = A03A_ADD(R(self + 0x1F4), F(1.0f)); W(self + 0x1F4, phase);
                if (A03A_LT(phase, F(60.0f))) {
                    W(bone(o, 4) + 0x7C, A03A_NEG(A03A_MUL(F(0.083333336f), phase)));
                    records = R(0x275B40); phase = R(self + 0x1F4);
                    W(R(records + 8) + 0x7C, A03A_MUL(F(0.083333336f), phase));
                } else {
                    WB(self + 5, 0); W(self + 0x1F0, !R(self + 0x1F0));
                    W(bone(o, 4) + 0x7C, F(-5.0f)); W(bone(o, 8) + 0x7C, F(5.0f));
                }
            } else {
                phase = A03A_SUB(R(self + 0x1F4), F(1.0f)); W(self + 0x1F4, phase);
                if (A03A_LT(0, phase)) {
                    W(bone(o, 4) + 0x7C, A03A_NEG(A03A_MUL(F(0.083333336f), phase)));
                    records = R(0x275B40); phase = R(self + 0x1F4);
                    W(R(records + 8) + 0x7C, A03A_MUL(F(0.083333336f), phase));
                } else {
                    WB(self + 5, 0); W(self + 0x1F0, !R(self + 0x1F0));
                    W(bone(o, 4) + 0x7C, 0); W(bone(o, 8) + 0x7C, 0);
                }
            }
            C(001C6380, self); break;
        default: break;
        }
        a03a_method(o, self); break;
    default: C(001AFC10, self); break;
    }
}

static void marker(A03A *o, int which, int active)
{
    int red;
    switch (which) {
    case 0: W(0x700038A0, F(550.1955f)); W(0x700038A4, F(94.9041f)); W(0x700038A8, F(413.1205f)); break;
    case 1: W(0x700038A0, F(550.2136f)); W(0x700038A4, F(-10.0981f)); W(0x700038A8, F(413.1457f)); break;
    case 2: W(0x700038A0, F(547.5613f)); W(0x700038A4, F(94.9041f)); W(0x700038A8, F(411.6271f)); break;
    default: W(0x700038A0, F(535.5598f)); W(0x700038A4, F(-10.0981f)); W(0x700038A8, F(423.631f)); break;
    }
    W(0x700038AC, F(1.0f));
    red = active ? ((R(0x275CA0) != 0) == (which == 1 || which == 2)) : B(0x70003B92) != 0;
    W(0x700038B0, red ? 128 : 0); W(0x700038B4, red ? 0 : 128);
    W(0x700038B8, 0); W(0x700038BC, 128); C(001F4BF0, 0x700038A0, 0x700038B0);
}

static void angle_marker(A03A *o, int lower)
{
    uint32_t angle = R(0x8106A0), pick;
    if (!(A03A_LE(angle, F(lower ? -2.268928f : -2.4434612f)) ||
          !A03A_LT(angle, F(lower ? 0.87266463f : 0.69813174f)))) return;
    pick = R(0x275CA4);
    if (lower) {
        if (pick == 0 || pick == 5) {
            W(0x700038A0, F(542.1862f)); W(0x700038A4, F(-1.7694f)); W(0x700038A8, F(421.4474f));
        } else if (pick == 1 || pick == 4) {
            W(0x700038A0, F(542.628f)); W(0x700038A4, F(-1.7694f)); W(0x700038A8, F(421.0331f));
        } else {
            W(0x700038A0, F(543.0248f)); W(0x700038A4, F(-1.7694f)); W(0x700038A8, F(420.6201f));
        }
    } else {
        if (pick == 0 || pick == 5) {
            W(0x700038A0, F(546.0836f)); W(0x700038A4, F(103.1611f)); W(0x700038A8, F(417.6306f));
        } else if (pick == 1 || pick == 4) {
            W(0x700038A0, F(545.6658f)); W(0x700038A4, F(103.1611f)); W(0x700038A8, F(418.0398f));
        } else {
            W(0x700038A0, F(545.2155f)); W(0x700038A4, F(103.1611f)); W(0x700038A8, F(418.4036f));
        }
    }
    W(0x700038AC, F(1.0f)); W(0x700038B0, 128); W(0x700038B4, 64);
    W(0x700038B8, 0); W(0x700038BC, 128); C(001F4E20, 0x700038A0, 0x700038B0, F(1.0f));
}

static void lift(A03A *o, uint32_t self, int which)
{
    int32_t r = 0, timer;
    uint32_t model, script, step;
    switch (B(self + 4)) {
    case 0:
        C(001B0FD0, self, &r); if (r) break;
        C(001C6380, self); WB(self + 4, 1); WB(self, 1); WB(self + 8, 1);
        model = which == 0 ? 0x828BB0 : which == 1 ? 0x828B90 : which == 2 ? 0x828FF0 : 0x828FD0;
        W(self + 0x30, model); break;
    case 1:
        if (which >= 2) WB(self, ((R(0x275CA0) != 0) != (which == 3)) ? 2 : 1);
        step = B(self + 5);
        if (!step) {
            if (B(self + 0xB) & 4) {
                script = which < 2 ? 0x8282D0 : ((R(0x275CA0) != 0) != (which == 3)) ? 0x828BD0 : 0x828CD0;
                C(001BA1A0, self + 0x1F0, script);
                W(0x700038A0, 0); W(0x700038A4, 0); W(0x700038A8, F(which < 2 ? 6.0f : 5.6f)); W(0x700038AC, F(1.0f));
                C(001B6F00, self, 0x700038A0, F(-2.9670596f));
                if (which == 1) W(0x275CA4, 0);
                WH(self + 0x28, 0); WB(self + 5, 1);
            }
            if (which < 2) marker(o, which, 0);
        } else {
            timer = a03a_s16(o, self + 0x28);
            if (timer < 120) {
                WH(self + 0x28, (uint32_t)(timer + 1));
                if (a03a_s16(o, self + 0x28) == 120) C(001FBD50, self, 0x19A, 0, F(300.0f), &r);
            }
            C(001BA1F0, self, &r);
            if (r) { if (which == 1) W(0x275CA4, 0); WB(self + 0xB, 0); WB(self + 5, 0); }
            if (which < 2) marker(o, which, 1);
        }
        C(001B17A0, self, &r); if (r && a03a_method(o, self)) return;
        if (which < 2) angle_marker(o, which == 1);
        else marker(o, which, 1);
        break;
    default: C(001AFC10, self); break;
    }
}

int em_area03_arrival_00825430(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    gate(&o, self, 0); return a03a_end(&o);
}

int em_area03_arrival_008256E0(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    gate(&o, self, 1); return a03a_end(&o);
}

int em_area03_arrival_00825980(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    lift(&o, self, 0); return a03a_end(&o);
}

int em_area03_arrival_00825DF0(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    lift(&o, self, 1); return a03a_end(&o);
}

int em_area03_arrival_00826340(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    lift(&o, self, 2); return a03a_end(&o);
}

int em_area03_arrival_008265D0(const EmArea03ArrivalHooks *h, uint32_t self, EmArea03ArrivalFault *fault)
{
    A03A o; if (a03a_begin(&o, h, fault)) return -1;
    lift(&o, self, 3); return a03a_end(&o);
}
