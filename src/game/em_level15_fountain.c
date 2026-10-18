/* Level-15 rising hazard and child bodies, from the decomp C. UNWIRED.
 * External calls require typed workers; borrowed memory is never retained. */
#include "em_level15_fountain_internal.h"
#define B(a) l15f_u8(o,(a))
#define H(a) l15f_u16(o,(a))
#define SH(a) l15f_s16(o,(a))
#define R(a) l15f_u32(o,(a))
#define W(a,v) l15f_w32(o,(a),(v))
#define WB(a,v) l15f_w8(o,(a),(v))
#define WH(a,v) l15f_w16(o,(a),(v))
#define F(v) l15f_bits(v)
#define C(fn,...) do { if (l15f_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

static uint32_t e001BECC0(L15F *o, uint32_t self)
{
    uint32_t state=B(self+4),work=self+0x1F0,player=0x8102B0,spawn=0,step,value,charge,cooldown;
    int32_t r=0,hit=0,timer;
    switch (state) {
    case 0:
        W(self+0xB4,L15F_ADD(R(self+0xB4),F(0.8f)));
        W(0x700038A0,0); W(0x700038A4,0); W(0x700038A8,0); W(0x700038AC,F(1.0f));
        C(001BEAC0,self,0x700038A0,100,0,&spawn); W(self+0x20,spawn);
        if (!R(self+0x20)) { WB(self+4,3); return 0; }
        C(001BEB70,self); C(001D0C80,self,R(0x28A500),&r);
        if (!r) {
            C(001D0D40,self,0x24E960,0x65,1); WH(work+2,0); C(001C62C0,self);
            W(self+0x58,R(0x28A504)); WB(self,1); W(self+0x30,0x275620); W(work+0x18,0x275628);
            WB(self+4,1); WH(work,0); WH(self+0x34,60); W(self+0x38,0); WB(self+0xB,0); W(work+0x1C,0);
        }
        return 0;
    case 1:
        C(001B17A0,self); WB(self+1,1); step=B(self+5);
        switch (step) {
        case 0:
            C(001BE5F0,player,self,work,&r);
            if (r) {
                C(00187EC0,8,0);
                if (!L15F_LT(R(player+0x38),F(0.4f))) {
                    WB(self+5,B(self+5)+1); W(self+0x38,0); C(001FBD50,self,0x435,0,F(300.0f));
                    C(001BEC40,self,work,1);
                } else C(001BEC40,self,work,0);
            } else C(001BEC40,self,work,0);
            break;
        case 1:
            C(001BEC40,self,work,1); value=L15F_ADD(R(self+0x38),F(0.6f)); W(self+0x38,value);
            if (L15F_LT(F(12.0f),value)) { W(self+0x38,F(12.0f)); WB(self+5,B(self+5)+1); WH(self+0x28,40); }
            C(001BE5F0,player,self,work,&r);
            if (r) {
                C(00187EC0,8,0);
                if (L15F_LT(F(4.0f),R(self+0x38)) && B(player)==1) {
                    W(player+0x224,F(5.0f)); WB(player,3); C(001EFD90,(int32_t)0x80000023,player+0xA0,player+0xC0);
                }
            }
            break;
        case 2:
            C(001BE5F0,player,self,work,&hit);
            if (hit) {
                C(00187EC0,8,0);
                if (B(player)==1) { W(player+0x224,F(5.0f)); WB(player,3); C(001EFD90,(int32_t)0x80000023,player+0xA0,player+0xC0); }
            }
            C(001BEC40,self,work,1); timer=SH(self+0x28);
            if (!timer) {
                if (!hit || L15F_LT(R(player+0x38),F(0.4f))) { WB(self+5,B(self+5)+1); C(001FBD50,self,0x436,0,F(300.0f)); }
            } else WH(self+0x28,(uint32_t)(timer-1));
            break;
        case 3:
            C(001BE5F0,player,self,work,&r); if (r) C(00187EC0,8,0);
            C(001BEC40,self,work,0); value=L15F_SUB(R(self+0x38),F(0.4f)); W(self+0x38,value);
            if (L15F_LT(value,0)) { W(self+0x38,0); WB(self+5,0); }
            break;
        default: break;
        }
        cooldown=R(work+0x1C); if (cooldown) W(work+0x1C,cooldown-1);
        charge=H(self+0x36);
        if (charge) {
            if (charge&0x5000) {
                if (!R(work+0x1C)) {
                    W(work+0x1C,60); if (H(self+0x36)&0x4000) C(001EFE00,(int32_t)0x80000027,self);
                } else WH(self+0x36,0);
            }
            timer=SH(self+0x36)&0xFFF; WH(self+0x34,(uint32_t)(SH(self+0x34)-timer));
            if (SH(self+0x34)<=0) {
                WB(self,2); WH(self+0x34,0); WB(self+4,2); WB(self+5,0);
                C(001C1500,self,0,F(20.0f),F(20.0f),F(10.0f));
                C(001FBD50,self,0x15D,0,F(300.0f)); C(001FBD50,self,0x43A,0,F(300.0f));
            } else { WB(self,1); WH(self+0x36,0); }
        }
        C(001C6380,self); C(001D0D60,R(self+0x90),F(1.0f),&r); WH(work,(uint32_t)r);
        C(001B17A0,self); (void)l15f_method(o,self); return 0;
    case 2:
        C(001C1570,self,&r); if (r) { WB(self+4,3); W(self+0x38,0); }
        value=L15F_SUB(R(self+0x64),F(0.01f)); W(self+0x64,value);
        if (L15F_LT(value,0)) W(self+0x64,0);
        value=L15F_SUB(R(self+0x38),F(0.4f)); W(self+0x38,value);
        if (L15F_LT(value,0)) W(self+0x38,0);
        W(self+0xB4,L15F_SUB(R(self+0xB4),F(0.01f)));
        C(001BEC40,self,work,0); C(001D0D60,R(self+0x90),F(1.0f),&r); WH(work,(uint32_t)r); C(001C6380,self);
        if (!L15F_EQ(R(self+0x64),0)) (void)l15f_method(o,self);
        break;
    case 3:
        C(001AF890,R(self+0x90),3); C(001B1190,(int32_t)B(self+0x9A)); C(001AFC10,self); break;
    default: break;
    }
    return 0;
}

static uint32_t e001BF3C0(L15F *o, uint32_t self)
{
    uint32_t state=B(self+4),parent=R(self+0x20),value,kind,other;
    int32_t r=0,model;
    if (state==2 || state==3) { C(001AFC10,self); return 0; }
    if (state!=0 && state!=1) return 0;
    if (!state) {
        if (B(parent+4)==3) { WB(self+4,3); return 0; }
        model=SH(0x275630+B(self+0xD)*2); C(001B1020,self,model,-1,0,&r);
        kind=B(self+0xD);
        if (kind==1) W(self+0xA4,L15F_SUB(R(self+0xA4),F(3.0f)));
        else if (kind==2) W(self+0xA4,L15F_SUB(R(self+0xA4),F(6.0f)));
    }
    other=B(parent+4);
    if (other==3) { WB(self+4,3); return 0; }
    if (other==2 && L15F_EQ(R(parent+0x38),0)) WB(self+4,3);
    kind=B(self+0xD); value=R(parent+0x38);
    switch (kind) {
    case 0: if (L15F_LT(F(6.0f),value)) value=F(6.0f); break;
    case 1: if (L15F_LT(F(9.0f),value)) value=F(9.0f); break;
    case 2: if (L15F_LT(F(12.0f),value)) value=F(12.0f); break;
    default: break;
    }
    W(self+0xB4,L15F_ADD(R(self+0xA4),value)); C(001C6380,self); (void)l15f_method(o,self); return 0;
}

int em_level15_fountain_001BECC0(const EmLevel15FountainHooks *h, uint32_t a0, EmLevel15FountainFault *fault)
{
    L15F o;
    if (l15f_begin(&o, h, fault)) return -1;
    (void)e001BECC0(&o, a0);
    return l15f_end(&o);
}

int em_level15_fountain_001BF3C0(const EmLevel15FountainHooks *h, uint32_t a0, EmLevel15FountainFault *fault)
{
    L15F o;
    if (l15f_begin(&o, h, fault)) return -1;
    (void)e001BF3C0(&o, a0);
    return l15f_end(&o);
}
