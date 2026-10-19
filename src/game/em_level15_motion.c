/* Creature dispatcher and movement, from the decomp C. UNWIRED.
 * State workers and math/render boundaries are explicit; local translated
 * motion routines execute directly. Original tables stay borrowed data. */
#include "em_level15_motion_internal.h"
#define B(a) l15m_u8(o,(a))
#define H(a) l15m_u16(o,(a))
#define SH(a) l15m_s16(o,(a))
#define R(a) l15m_u32(o,(a))
#define W(a,v) l15m_w32(o,(a),(v))
#define WB(a,v) l15m_w8(o,(a),(v))
#define WH(a,v) l15m_w16(o,(a),(v))
#define F(v) l15m_bits(v)
#define C(fn,...) do { if (l15m_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

static uint32_t e00140C30(L15M *o, uint32_t self, uint32_t work, uint32_t sp)
{
    uint32_t state,table=0,phase,trig=0,speed,pos,span,cell;
    uint32_t whole=sp-8,previous=sp-4;
    int32_t kind,r=0;
    if ((B(work+0x81)&0x7F)!=2) {
        speed=L15M_ADD(R(work+0x48),F(-0.1f)); W(work+0x48,speed);
        W(self+0xB4,L15M_ADD(R(self+0xB4),speed));
    }
    if (!(B(work+0x81)&128) && L15M_LE(R(work+0x5C),F(12.5f))) {
        C(001B1560,self,0x810360,F(1.1344640254974365f),&r);
        if (r) { WH(self+0x52,H(self+0x52)|1); return 0; }
    }
    state=B(work+0x81)&0x7F;
    if (!state) {
        speed=R(work+0x50); W(work+0x4C,L15M_ADD(R(work+0x4C),speed));
        C(0011E2A8,R(self+0xC4),&trig); speed=R(work+0x4C); pos=R(self+0xB0); W(self+0xB0,L15M_ADD(pos,L15M_MUL(speed,trig)));
        C(0011DE90,R(self+0xC4),&trig); speed=R(work+0x4C); pos=R(self+0xB8); W(self+0xB8,L15M_ADD(pos,L15M_MUL(speed,trig)));
    } else if (state==1) {
        kind=SH(self+0x2C); if ((uint32_t)kind&0x8000) return 0;
        switch (kind) {
        case 3: table=0x2451E0; break; case 4: table=0x245250; break;
        case 5: table=0x2452C0; break; case 8: table=0x245380; break;
        case 19: table=0x245400; break; case 21: table=0x2454C0; break;
        case 27: table=0x245550; break; case 35: table=0x245610; break;
        default: WB(work+0x81,0); return 0;
        }
        C(001C6160,self,&r); phase=L15M_SUB(L15M_CVT_S_W(r),R(self+0x3C));
        C(0011E0A8,whole,phase);
        C(0011E0A8,previous,L15M_ADD(F(1.0f),L15M_SUB(phase,R(work+0x40))));
        span=L15M_SUB(R(whole),R(previous)); W(whole,span);
        if (L15M_LT(span,0)) return 0;
        do {
            C(001281C0,phase,&r); cell=table+(uint32_t)r*4;
            W(work+0x4C,R(cell));
            C(0011E2A8,R(self+0xC4),&trig); speed=R(cell); pos=R(self+0xB0); W(self+0xB0,L15M_ADD(pos,L15M_MUL(speed,trig)));
            C(0011DE90,R(self+0xC4),&trig); speed=R(cell); pos=R(self+0xB8); W(self+0xB8,L15M_ADD(pos,L15M_MUL(speed,trig)));
            phase=L15M_SUB(phase,F(1.0f));
            if (L15M_LT(phase,0)) { C(001C6160,self,&r); phase=L15M_CVT_S_W((uint32_t)r-1); }
            span=L15M_SUB(R(whole),F(1.0f)); W(whole,span);
        } while (!L15M_LE(span,0) && !l15m_failed(o));
    }
    return 0;
}

static uint32_t e00140F80(L15M *o, uint32_t self, uint32_t work)
{
    uint32_t phase,height,x,y,z;
    int32_t r=0;
    WB(work+0x80,0);
    if (L15M_LT(R(work+0x48),0)) {
        W(0x700038A0,0); W(0x700038B0,0); W(0x700038A4,F(3.0f)); W(0x700038B4,F(3.0f));
        W(0x700038B8,F(3.0f)); W(0x700038D0,F(3.0f)); W(0x700038D4,F(3.0f)); W(0x700038D8,0);
        W(0x700038A8,F(-3.0f)); W(0x700038C0,F(-3.0f)); W(0x700038C4,F(3.0f)); W(0x700038C8,0);
        W(0x700038AC,F(1.0f)); W(0x700038BC,F(1.0f)); W(0x700038CC,F(1.0f)); W(0x700038DC,F(1.0f));
        C(001029C0,0x700036A0); C(00102BB0,0x700036A0,0x700036A0,R(self+0xC4));
        for (uint32_t i=0;i<4;++i) { x=0x700038A0+i*0x10; C(001026A0,x,0x700036A0,x); }
        for (uint32_t i=0;i<4;++i) { x=0x700038A0+i*0x10; C(001028B8,x,self+0xB0,x); }
        C(001B3390,self,0x700038A0,0x700038B0,F(0.7853982f),&r);
        if (!r) C(001B3390,self,0x700038B0,0x700038A0,F(0.7853982f),&r);
        if (r) WB(work+0x80,B(work+0x80)|1);
        if (!B(work+0x80)) {
            C(001B3390,self,0x700038C0,0x700038D0,F(0.7853982f),&r);
            if (!r) C(001B3390,self,0x700038D0,0x700038C0,F(0.7853982f),&r);
            if (r) WB(work+0x80,B(work+0x80)|1);
        }
    } else {
        C(001029C0,0x700036A0); C(00102BB0,0x700036A0,0x700036A0,R(self+0xC4));
        for (uint32_t i=0;i<4;++i) {
            C(001026A0,0x700038A0,0x700036A0,0x245900+(i+1)*0x10);
            C(001028B8,0x700038A0,self+0xB0,0x700038A0);
            C(001B32F0,self,0x700038A0,F(0.7853982f),&r);
            if (r) { WB(work+0x80,B(work+0x80)|1); break; }
        }
    }
    if (B(work+0x80)&1) WH(self+0x52,H(self+0x52)|1);
    phase=R(work+0x48);
    if (L15M_LT(phase,0)) {
        C(001029C0,0x700036A0); C(00102BB0,0x700036A0,0x700036A0,R(self+0xC4));
        W(0x700038B0,0); W(0x700038B4,F(-10.0f)); W(0x700038B8,0); W(0x700038BC,F(1.0f));
        for (uint32_t i=0;i<5;++i) {
            C(001026A0,0x700038A0,0x700036A0,0x245900+i*0x10);
            C(001028B8,0x700038A0,self+0xB0,0x700038A0);
            C(001B3440,self,0x700038A0,0x700038B0,F(0.7853982f),&r);
            if (r) { W(work+0x48,0); WB(work+0x80,B(work+0x80)|2); break; }
        }
        if (!(B(work+0x80)&2) && B(self+4)==1 && !B(work+0x85)) {
            C(001B2F70,self+0xB0,0x700038A0,&r);
            if (!r) W(0x700038A0,L15M_SUB(R(self+0xB4),F(13.0f)));
            height=L15M_SUB(R(self+0xB4),R(0x700038A0));
            if (!L15M_LT(height,F(13.0f)) && L15M_LT(R(work+0x48),F(-0.3f))) { WB(self+5,5); WB(self+6,0); }
        }
        x=R(self+0xB0); W(0x700038A0,x); y=R(self+0xB4); W(0x700038A4,y); z=R(self+0xB8); W(0x700038A8,z);
        W(0x700038AC,F(1.0f)); W(0x700038A4,L15M_ADD(R(0x700038A4),F(18.0f)));
        WB(work+0x86,B(work+0x86)&~1u); C(0019B6C0,0x700038A0,self+0xB0,&r);
        if (r && B(R(0x700031D0)+0x1A)==0x5B) {
            WB(work+0x86,B(work+0x86)|1); W(work+0x60,R(0x700031B4));
            if (!B(work+0x87) && !L15M_EQ(R(work+0x4C),0)) {
                C(00122BB8,&r); WB(work+0x87,((uint32_t)r>>9&7)+7);
                W(0x700038A0,R(self+0xB0)); W(0x700038A4,R(self+0xB4)); W(0x700038A8,R(self+0xB8));
                W(0x700038AC,F(1.0f)); W(0x700038A4,R(work+0x60));
                C(001EFD90,(int32_t)0x8000001D,0x700038A0,self+0xC0);
            }
        }
    } else if (!L15M_LE(phase,0)) {
        W(0x700038A0,R(self+0xB0)); W(0x700038A4,R(self+0xB4)); W(0x700038A8,R(self+0xB8)); W(0x700038AC,R(self+0xBC));
        W(0x700038B0,0); W(0x700038B4,F(15.0f)); W(0x700038B8,0); W(0x700038BC,F(1.0f));
        W(0x700038A4,L15M_ADD(R(0x700038A4),F(20.002f)));
        C(0019AB20,self,0x700038A0,0x700038B0,0x80000007,&r);
        if (r) { W(work+0x48,0); WB(work+0x80,B(work+0x80)|4); }
    }
    return 0;
}

static uint32_t e0013D600(L15M *o, uint32_t self, uint32_t work, uint32_t sp)
{
    int32_t r=0;
    uint32_t distance=0;
    WB(work+0x8E,0); C(001B2140,self,&r); if (!r) return 0;
    WB(work+0x8E,1); C(00102948,0x700038A0,self+0xB0); C(00102948,0x700038B0,0x810360);
    W(0x700038B4,0); W(0x700038A4,0); C(001B15D0,0x700038A0,0x700038B0,&distance);
    W(work+0x5C,distance); WB(work+0x85,0);
    switch (B(self+5)) {
    case 0: C(0013D850,self,work); break; case 1: C(0013D980,self,work); break;
    case 2: C(0013DB70,self,work); break; case 3: C(0013DC60,self,work); break;
    case 4: C(0013DD40,self,work); break; case 5: C(0013EB90,self,work); break;
    case 6: C(0013E6D0,self,work); break; case 7: C(0013EDB0,self,work); break;
    case 8: C(0013F240,self,work); break; case 9: C(0013F770,self,work); break;
    case 10: C(00140440,self,work); break; default: break;
    }
    WB(self,B(work+0x88)?3:1);
    if (B(work+0x8D)) { WB(self+0x5D,0x81); C(001B4810,self); }
    else WB(self+0x5D,1);
    WB(self+0xA,0); C(00141A30,self,work); C(00141C90,self,work);
    (void)e00140C30(o,self,work,sp-0x30); (void)e00140F80(o,self,work);
    C(001C64F0,self,R(work+0x40),&r); W(work+0x30,(uint32_t)r);
    C(00131ED0,self); C(001C68C0,self); C(001B17A0,self,&r); if (r) (void)l15m_method(o,self); return 0;
}

int em_level15_motion_0013D600(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, EmLevel15MotionFault *fault)
{
    L15M o;
    if (l15m_begin(&o, h, fault)) return -1;
    (void)e0013D600(&o, a0, a1, a2);
    return l15m_end(&o);
}

int em_level15_motion_00140C30(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, EmLevel15MotionFault *fault)
{
    L15M o;
    if (l15m_begin(&o, h, fault)) return -1;
    (void)e00140C30(&o, a0, a1, a2);
    return l15m_end(&o);
}

int em_level15_motion_00140F80(const EmLevel15MotionHooks *h, uint32_t a0, uint32_t a1, EmLevel15MotionFault *fault)
{
    L15M o;
    if (l15m_begin(&o, h, fault)) return -1;
    (void)e00140F80(&o, a0, a1);
    return l15m_end(&o);
}
