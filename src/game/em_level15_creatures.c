/* Level-15 creature routines from decomp C; original-instruction evidence
 * is required independently for each entry. UNWIRED. */
#include "em_level15_creatures_internal.h"
#define B(a) l15c_u8(o,(a))
#define H(a) l15c_u16(o,(a))
#define SH(a) l15c_s16(o,(a))
#define R(a) l15c_u32(o,(a))
#define W(a,v) l15c_w32(o,(a),(v))
#define WB(a,v) l15c_w8(o,(a),(v))
#define WH(a,v) l15c_w16(o,(a),(v))
#define F(v) l15c_bits(v)
#define C(fn,...) do { if (l15c_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

static uint32_t e0012F6C0(L15C *o, uint32_t self, uint32_t work)
{
    uint32_t state=B(self+6),t;
    int32_t r=0;
    switch (state) {
    case 0:
        WB(self+6,1); WB(work+0x63,0); WH(work+0x54,0); W(work+0x34,F(1.0f));
        C(001C67E0,self,8,F(10.0f),0);
        /* fall through */
    case 1:
        if (H(work+0x58)&0x8000) break;
        WB(self+6,B(self+6)+1);
        /* fall through */
    case 2:
        switch (SH(self+0x2C)) {
        case 8:
            if (H(work+0x58)&0x7000) C(001C67E0,self,9,F(1.0f),0);
            break;
        case 9:
            W(0x700038A0,0); W(0x700038A4,F(3.0f)); W(0x700038A8,F(45.0f)); W(0x700038AC,F(1.0f));
            C(00131F20,self,0x700038A0,0x700038A0); C(001028B8,0x700038A0,0x700038A0,self+0xB0);
            C(001B3250,self,0x700038A0,F(15.0f),&r);
            if (!r) {
                C(001B1560,self,0x810360,F(1.0471976f),&r);
                if (r) C(001B13F0,F(45.0f),0x810360,self+0xB0,&r);
                else r=1;
            }
            if (r) C(001C67E0,self,10,F(1.0f),0);
            break;
        case 10:
            if (H(work+0x58)&0x1000) { WB(self+5,0); WB(self+6,0); WB(work+0x62,0); W(work+0x34,F(1.0f)); }
            break;
        default: break;
        }
        if (B(work+0x69)&1) {
            t=H(work+0x54);
            if (t) WH(work+0x54,t-1);
            else {
                C(00122BB8,&r); WH(work+0x54,((uint32_t)r>>9&7)+5);
                W(0x700038A0,R(self+0xB0)); W(0x700038A4,R(self+0xB4)); W(0x700038A8,R(self+0xB8));
                W(0x700038AC,F(1.0f)); W(0x700038A4,R(work+0x40));
                C(001EFD90,(int32_t)0x8000001D,0x700038A0,self+0xC0);
            }
        }
        C(00132490,self,work); break;
    default: break;
    }
    return 0;
}

static uint32_t e0013D490(L15C *o, uint32_t self, uint32_t work)
{
    int32_t r=0;
    WB(self+4,B(self+4)+1); WB(self,1); WB(work+0x8E,1); W(work+0x40,F(1.0f));
    WB(work+0x8F,B(self+0xD)); W(self+0x30,0x2753B8);
    for (uint32_t i=0;i<0x20;i+=4) W(work+i,R(self+0xB0+i));
    W(self+0x58,R(0x28A668));
    if (B(0x810808)==255) WB(self+0xD,B(self+0xD)|128);
    if (B(self+0xD)&128) {
        WH(self+0x34,B(0x81070A)?200:100);
        W(self+0x60,F(1.1f)); W(self+0x64,F(1.1f)); W(self+0x68,F(1.1f)); W(self+0x6C,F(1.0f));
        C(001B10B0,self,0x77,0x78,&r);
    } else {
        WH(self+0x34,B(0x81070A)?100:60); C(001B10B0,self,0x75,0x78,&r);
    }
    if (!r) C(001C63E0,self,0);
    return 0;
}

static uint32_t e00141A30(L15C *o, uint32_t self, uint32_t work)
{
    int32_t damage,charge,hp,r=0;
    if (!SH(self+0x36)) return 0;
    if (B(work+0x88) || B(work+0x89)) { WH(self+0x36,0); return 0; }
    WB(self,2); WH(work+0x70,240); WB(work+0x88,255);
    damage=SH(self+0x36); charge=damage&0xFFF; if (damage&0x8000) charge*=5;
    if (B(work+0x8A)) WH(work+0x74,(uint32_t)(SH(work+0x74)+charge));
    else WH(work+0x74,(uint32_t)charge);
    WB(work+0x8A,25);
    if (!B(work+0x8B) && (H(self+0x36)&0x4000)) { WB(work+0x8B,60); C(001EFE00,(int32_t)0x80000027,self,&r); }
    hp=SH(self+0x34);
    if (hp<=charge) {
        WH(self+0x34,0); WB(work+0x8C,0); WB(self+4,2); WB(self+5,2); WB(self+6,0);
        C(001B4CF0,self);
        if (!(B(work+0x80)&2) || (H(self+0x36)&0xA000)) WB(self+5,1);
        return 1;
    }
    WH(self+0x34,(uint32_t)(hp-charge)); damage=SH(self+0x36);
    if (damage&0x2000) { WB(self+4,2); WB(self+5,1); WB(self+6,0); return 1; }
    if ((B(self+4)==1 && SH(work+0x74)>=25) || (damage&0x8000)) {
        WB(self+4,2); WB(self+6,0); WH(work+0x74,0); WB(self+5,(B(work+0x80)&2)?0:1); return 1;
    }
    WB(work+0x88,(damage&0x5000)?30:0); WB(self,1); WH(self+0x36,0);
    if (B(self+4)==1 && B(self+5)<5) { WB(self+5,6); WB(self+6,0); }
    return 0;
}

static uint32_t e00153B50(L15C *o, uint32_t self, uint32_t work)
{
    uint32_t state=B(self+5),value,trig=0;
    int32_t hp,r=0;
    switch (state) {
    case 0:
        hp=SH(self+0x34)-SH(self+0x36); WH(self+0x34,(uint32_t)hp);
        if ((int16_t)hp<=0) {
            WB(self+5,2); W(work+4,0); W(work+8,F(-0.01f)); W(work+12,F(-0.03f)); WB(0x8107FB,2);
            C(001FBD50,self,0x7D8,0,F(300.0f)); C(001C67E0,self,0x34,F(10.0f),0);
        } else {
            WB(self+5,B(self+5)+1); W(work+12,0); W(work+8,0); W(work+4,0);
            C(001FBD50,self,0x7D4,0,F(300.0f)); C(001C67E0,self,0x37,F(10.0f),0);
        }
        break;
    case 1:
        if (R(work)&0x1000) {
            WB(self+4,1); WB(self+5,0); WB(self,1); WH(self+0x36,0);
            C(001C67E0,self,0x33,F(10.0f),0);
        }
        break;
    case 2:
        value=R(work+8); value=L15C_ADD(R(work+4),value); W(work+4,value);
        if (L15C_LT(value,F(-5.0f))) { W(work+4,F(-5.0f)); W(work+8,0); }
        value=R(work+4); W(self+0xB4,L15C_ADD(R(self+0xB4),value));
        C(0011E2A8,R(self+0xC4),&trig); value=R(work+12); W(self+0xB0,L15C_ADD(R(self+0xB0),L15C_MUL(value,trig)));
        C(0011DE90,R(self+0xC4),&trig); value=R(work+12); W(self+0xB8,L15C_ADD(R(self+0xB8),L15C_MUL(value,trig)));
        W(0x700038A0,0); W(0x700038A4,F(-2.0f)); W(0x700038A8,0); W(0x700038AC,F(1.0f));
        C(001028B8,0x700038A0,0x700038A0,self+0xB0);
        W(0x700038B0,0); W(0x700038B4,R(work+4)); W(0x700038B8,0); W(0x700038BC,F(1.0f));
        C(0019AB20,self,0x700038A0,0x700038B0,0x80000007,&r);
        if (r) {
            WB(self+5,B(self+5)+1); C(001FBD50,self,0x7D5,0,F(300.0f)); C(001C67E0,self,0x36,F(10.0f),0);
        }
        break;
    case 3:
        if (R(work)&0x1000) {
            WB(self+5,state+1); WB(work+0x10,0); C(001FBD50,self,0x7D7,0,F(300.0f));
            C(001EFE00,(int32_t)0x8000001E,self,&r); if (!r) WB(self+4,3);
        }
        break;
    default: break;
    }
    C(001C64F0,self,F(1.0f),&r); W(work,(uint32_t)r); C(00131ED0,self); C(001C68C0,self);
    C(001B17A0,self,&r); if (r) (void)l15c_method(o,self); return 0;
}

/* AREA03 sub 0's creature: the per-frame update, by the scene byte 3B8D. */
static uint32_t e00140690(L15C *o, uint32_t e, uint32_t st)
{
    int32_t r=0;
    switch (B(e+5)) {
    case 0: C(00140760,e,st); break;
    case 1: C(001408D0,e,st); break;
    case 2: C(00140A90,e,st); break;
    default: break;
    }
    C(00140C30,e,st); C(00140F80,e,st);
    C(001C64F0,e,R(st+0x40),&r); W(st+0x30,(uint32_t)r);
    C(00131ED0,e); C(001C68C0,e); C(001B17A0,e,&r);
    (void)l15c_method(o,e);
    return 0;
}
static uint32_t e00140C00(L15C *o, uint32_t obj)
{
    C(001B1190,(int32_t)B(obj+0x9A)); C(001AFC10,obj); return 0;
}
static uint32_t e0013D2D0(L15C *o, uint32_t e)
{
    const uint32_t st=e+0x1F0;
    int32_t r=0;
    uint32_t v;
    switch (B(0x70003B8Du)) {
    case 1:
        C(001B2140,e,&r);
        if (!r || !B(e+4)) return 0;
        (void)l15c_method(o,e);
        return 0;
    case 2: case 3: return 0;
    default: break;
    }
    WH(e+0x52,0); WB(e+0x5E,1);
    switch (B(e+4)) {
    case 0: (void)e0013D490(o,e,st); break;
    case 1: C(0013D600,e,st); break;
    case 2: (void)e00140690(o,e,st); break;
    case 3: (void)e00140C00(o,e); return 0;
    default: break;
    }
    if (l15c_failed(o)) return 0;
    if ((v=H(st+0x70))) WH(st+0x70,v-1);
    if ((v=H(st+0x76))) WH(st+0x76,v-1);
    if ((v=B(st+0x8B))) WB(st+0x8B,v-1);
    if ((v=B(st+0x87))) WB(st+0x87,v-1);
    if ((v=H(st+0x72))) WH(st+0x72,v-1);
    if ((v=B(st+0x88))) WB(st+0x88,v-1);
    WH(e+0x54,0);
    if (B(st+0x8E)) C(001B5360,e);
    C(001B0D80,e,&r);
    return 0;
}
static uint32_t e0013D850(L15C *o, uint32_t self, uint32_t st)
{
    const uint32_t sub=B(self+6);
    uint32_t v;
    switch (sub) {
    case 0:
        WB(self+6,sub+1); W(st+0x40,F(1.0f)); W(st+0x48,0); W(st+0x50,0); W(st+0x4C,0);
        C(001C67E0,self,0,F(5.0f),0);
        break;
    case 1:
        v=(uint32_t)SH(self+0x56);
        if (v) {
            if (em_ee_c_le_bits(R(st+0x5C),em_ee_cvt_s_w_bits(v)) || (B(self+0xA)&1)) WH(self+0x56,0);
        } else {
            C(001418F0,self,st);
            if (H(st+0x70)) {
                WB(self+6,B(self+6)+1);
                C(001C67E0,self,1,0,0);
            }
        }
        break;
    case 2:
        if (R(st+0x30)&0x1000) { WB(self+5,4); WB(self+6,0); }
        break;
    default: break;
    }
    return 0;
}
static uint32_t e00141C90(L15C *o, uint32_t e, uint32_t s)
{
    if (B(e+0xD)&0x80) return 0;
    if (H(s+0x76)) return 0;
    if (!H(s+0x70)) return 0;
    if (!H(e+0x54)) return 0;
    if (!(B(s+0x80)&2)) return 0;
    if (B(e+4)!=1) return 0;
    const uint32_t v=B(e+5);
    if (v>=7 || v==5) return 0;
    WB(e+5,10); WB(e+6,0);
    return 0;
}

#define ENTRY2(name) \
int em_level15_creatures_##name(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault) \
{ \
    L15C o; \
    if (l15c_begin(&o, h, fault)) return -1; \
    (void)e##name(&o, a0, a1); \
    return l15c_end(&o); \
}
ENTRY2(0013D850)
ENTRY2(00140690)
ENTRY2(00141C90)
#undef ENTRY2
int em_level15_creatures_0013D2D0(const EmLevel15CreaturesHooks *h, uint32_t a0, EmLevel15CreaturesFault *fault)
{
    L15C o;
    if (l15c_begin(&o, h, fault)) return -1;
    (void)e0013D2D0(&o, a0);
    return l15c_end(&o);
}
int em_level15_creatures_00140C00(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault)
{
    L15C o;
    (void)a1;   /* 0013D2D0 leaves st in a1; the original never reads it */
    if (l15c_begin(&o, h, fault)) return -1;
    (void)e00140C00(&o, a0);
    return l15c_end(&o);
}

int em_level15_creatures_0012F6C0(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault)
{
    L15C o;
    if (l15c_begin(&o, h, fault)) return -1;
    (void)e0012F6C0(&o, a0, a1);
    return l15c_end(&o);
}

int em_level15_creatures_0013D490(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault)
{
    L15C o;
    if (l15c_begin(&o, h, fault)) return -1;
    (void)e0013D490(&o, a0, a1);
    return l15c_end(&o);
}

int em_level15_creatures_00141A30(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, int32_t *result, EmLevel15CreaturesFault *fault)
{
    L15C o;
    if (!result || l15c_begin(&o, h, fault)) return -1;
    uint32_t value = e00141A30(&o, a0, a1);
    if (l15c_failed(&o)) return -1;
    *result = (int32_t)value;
    return 0;
}

int em_level15_creatures_00153B50(const EmLevel15CreaturesHooks *h, uint32_t a0, uint32_t a1, EmLevel15CreaturesFault *fault)
{
    L15C o;
    if (l15c_begin(&o, h, fault)) return -1;
    (void)e00153B50(&o, a0, a1);
    return l15c_end(&o);
}
