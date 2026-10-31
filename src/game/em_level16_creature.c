/* The AREA03 creature's remaining behaviour states, from the decomp C (and the
 * original instructions for 0013F240, whose C is a near miss). UNWIRED.
 * Workers stay explicit; the set's internal call (001416D0) executes directly.
 * Original tables stay borrowed data. */
#include "em_level16_creature_internal.h"
#define B(a) l16c_u8(o,(a))
#define SB(a) l16c_s8(o,(a))
#define H(a) l16c_u16(o,(a))
#define SH(a) l16c_s16(o,(a))
#define R(a) l16c_u32(o,(a))
#define W(a,v) l16c_w32(o,(a),(v))
#define WB(a,v) l16c_w8(o,(a),(v))
#define WH(a,v) l16c_w16(o,(a),(v))
#define F(v) l16c_bits(v)
#define C(fn,...) do { if (l16c_c_##fn(o, __VA_ARGS__)) return 0; } while (0)
/* A translated helper; a fault it latched ends the caller too. */
#define SUB(call) do { (void)(call); if (l16c_failed(o)) return 0; } while (0)
#define RAND(r) do { if (l16c_c_00122BB8(o,&(r))) return 0; } while (0)
/* The cue and clip calls every state makes. */
#define CUE(self,id) C(001FBD50,(self),(id),0,F(300.0f))
#define CLIP(self,id,blend,frame) C(001C67E0,(self),(id),(blend),(frame))
#define PI F(3.1415927f)
#define V0 0x700038A0u
#define V1 0x700038B0u
#define M0 0x700036A0u
#define ORIGIN(self) ((self)+0xB0)
#define PLAYER 0x8102B0u
#define TARGET 0x810360u

/* Four words of a scratch vector, first to last. */
static void put4(L16C *o, uint32_t at, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    W(at,x); W(at+4,y); W(at+8,z); W(at+0xC,w);
}

/* Effect 0x80000016 and 001E8B90 at the point z ahead of self, at the height
 * in work+0x60. */
static uint32_t effect_ahead(L16C *o, uint32_t self, uint32_t work, uint32_t z)
{
    put4(o,V0,0,0,z,F(1.0f));
    C(001B2B10,self,V0,V0); C(001028B8,V0,ORIGIN(self),V0);
    W(V0+4,R(work+0x60));
    C(001EFD90,(int32_t)0x80000016,V0,self+0xC0); C(001E8B90,V0,F(5.0f));
    return 0;
}

/* 001B3390 between self's origin raised 10 and the point z ahead of it; a
 * hit clears work+0x4C. */
static uint32_t wall_probe(L16C *o, uint32_t self, uint32_t work, uint32_t z)
{
    uint32_t y;
    int32_t r=0;
    C(00102948,V0,ORIGIN(self));
    W(V1,0); W(V1+4,0);
    y=R(V0+4);
    W(V1+8,z); W(V1+0xC,F(1.0f));
    W(V0+4,L16C_ADD(y,F(10.0f)));
    C(001B2B10,self,V1,V1); C(001028B8,V1,V1,V0);
    C(001B3390,self,V0,V1,F(0.7853982f),&r);
    if (r) W(work+0x4C,0);
    return 0;
}

/* 001416D0: 001B2BF0 at the point (0,3,13) ahead of self. With bit 2 of its
 * result, work+0x83 comes from 001B3670 over two vectors in this frame.
 * Otherwise work+0x83 = 2 and work+0x44 comes from 001B37D0, unless bit 0 is
 * set and self's height less 20 is below the height at 0x700038D0: only that
 * case returns 0. */
static uint32_t e001416D0(L16C *o, uint32_t self, uint32_t work, uint32_t sp)
{
    uint32_t a=sp-0x20,b=sp-0x10,p,x,bx,by,bz,heading=0;
    int32_t flags=0,r=0;
    put4(o,V0,0,F(3.0f),F(13.0f),F(1.0f));
    C(001B2B10,self,V0,V0); C(001028B8,V0,V0,ORIGIN(self));
    C(001B2BF0,self,V0,0x700038D0,F(0.785398185f),&flags);
    if (flags&4) {
        p=R(0x700031D0);
        x=R(p+0x24); bx=R(0x700031B0); W(a,x);
        x=R(p+0x28); by=R(0x700031B4); W(a+4,x);
        x=R(p+0x2C); bz=R(0x700031B8);
        W(b,bx); W(b+4,by); W(a+8,x); W(b+8,bz);
        W(a+0xC,F(1.0f)); W(b+0xC,F(1.0f));
        C(001B3670,self,a,b,&r); WB(work+0x83,(uint32_t)r);
        return 1;
    }
    if ((flags&1) && L16C_LT(L16C_SUB(R(self+0xB4),F(20.0f)),R(0x700038D0))) return 0;
    WB(work+0x83,2);
    C(001B37D0,self,F(10.0f),F(20.0f),&heading); W(work+0x44,heading);
    return 1;
}

/* self+5 == 1 in 0013D600's table: clip 9, repeated work+0x20 times (1 when
 * work+0x70 is set, else a value of the table at 0x2458E0). */
static uint32_t e0013D980(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),f;
    int32_t r=0;
    if (st==0) {
        WB(self+6,st+1); W(work+0x40,F(1.0f)); W(work+0x48,0);
        if (SH(work+0x70)) W(work+0x20,1);
        else { RAND(r); W(work+0x20,(uint32_t)SB(0x2458E0+((uint32_t)r>>16&0xF))); }
        W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,9,F(10.0f),0);
    } else if (st==1) {
        C(001418F0,self,work);
        if ((B(self+0xA)&1) || (SH(work+0x70) && B(0x81050C)>=2)) { WB(self+5,6); WB(self+6,0); return 0; }
        if (!(SH(self+0x2C)&0x8000)) {
            f=R(self+0x3C);
            if (L16C_EQ(f,F(77.0f))||L16C_EQ(f,F(64.0f))||L16C_EQ(f,F(52.0f))||L16C_EQ(f,F(19.0f)))
                CUE(self,0x805);
        }
        if (R(work+0x30)&0x1000) {
            f=R(work+0x20)-1; W(work+0x20,f);
            if (f) CLIP(self,9,0,0);
            else { WB(self+5,4); WB(self+6,0); }
        }
    }
    return 0;
}

/* self+5 == 2: clip 0xB, turning towards 0x810360 at one degree a tick. */
static uint32_t e0013DB70(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),x,z,goal=0,yaw;
    if (st==0) {
        WB(self+6,st+1); W(work+0x40,F(1.0f));
        W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,0xB,F(15.0f),0);
        CUE(self,0x808);
    } else if (st==1) {
        x=R(0x810360); z=R(0x810368);
        C(001B1240,ORIGIN(self),x,z,&goal); W(work+0x44,goal);
        yaw=R(self+0xC4); goal=R(work+0x44);
        C(001B12B0,goal,yaw,F(0.017453292f),&yaw); W(self+0xC4,yaw);
        if (R(work+0x30)&0x1000) { WB(self+5,4); WB(self+6,0); }
    }
    return 0;
}

/* self+5 == 3: clip 10, with cue 0x806 at frame 62. */
static uint32_t e0013DC60(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6);
    if (st==0) {
        WB(self+6,st+1); W(work+0x40,F(1.0f));
        W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,10,F(10.0f),0);
    } else if (st==1) {
        if (!(SH(self+0x2C)&0x8000) && L16C_EQ(R(self+0x3C),F(62.0f))) CUE(self,0x806);
        if (R(work+0x30)&0x1000) { WB(self+5,4); WB(self+6,0); }
    }
    return 0;
}

/* (0,3,13,1) rotated by M0 and carried to self's origin. */
static uint32_t ahead(L16C *o, uint32_t self)
{
    put4(o,V0,0,F(3.0f),F(13.0f),F(1.0f));
    C(001026A0,V0,M0,V0); C(001028B8,V0,V0,ORIGIN(self));
    return 0;
}

/* The magnitude of work+0x44 less self's yaw plus pi, each wrapped by 001B1470. */
static uint32_t off_course(L16C *o, uint32_t self, uint32_t work, uint32_t *angle)
{
    uint32_t a=0;
    C(001B1470,L16C_ADD(PI,R(self+0xC4)),&a);
    C(001B1470,L16C_SUB(R(work+0x44),a),&a);
    C(0011DF78,a,angle);
    return 0;
}

/* Self's origin and the four words at work, with both heights cleared. */
static void sight_vectors(L16C *o, uint32_t self, uint32_t work)
{
    W(V0,R(self+0xB0)); W(V0+4,R(self+0xB4)); W(V0+8,R(self+0xB8)); W(V0+0xC,R(self+0xBC));
    W(V1,R(work)); W(V1+4,R(work+4)); W(V1+8,R(work+8)); W(V1+0xC,R(work+0xC));
    W(V1+4,0); W(V0+4,0);
}

/* self+5 == 4. Sub-state 0 draws a heading, 1 steers with 001416D0 and the
 * sight tests, 2 turns with the two side probes. The shared tail runs the
 * timers and cues and chooses the next self+5. */
static uint32_t e0013DD40(L16C *o, uint32_t self, uint32_t work, uint32_t sp)
{
    uint32_t st=B(self+6),rate,deg,spin,value=0,x,z;
    int32_t r=0;
    switch (st) {
    case 0:
        WB(self+6,st+1); WB(work+0x81,0); W(work+0x4C,0x3E19999Au);
        RAND(r); W(work+0x20,((uint32_t)r>>12&0xFF)+0x78);
        W(work+0x24,0); W(work+0x28,0); WB(work+0x82,0);
        RAND(r);
        value=L16C_DIV(L16C_MUL(F(6.2831855f),L16C_CVT_S_W((uint32_t)r>>15&0xFF)),F(255.0f));
        C(001B1470,value,&value); W(work+0x44,value);
        if (((uint32_t)SH(self+0x2C)&0xFFFF7FFFu)!=2) CLIP(self,2,F(5.0f),0);
        break;
    case 1:
        rate=F(1.8f);
        if (e001416D0(o,self,work,sp-0x40)) {
            WB(self+6,B(self+6)+1);
            if (SB(work+0x83)==2) {
                SUB(off_course(o,self,work,&value));
                if (L16C_LE(value,F(0.3926991f))) {
                    r=(int8_t)(SB(work+0x82)+1); WB(work+0x82,(uint32_t)r);
                    if (r>2) {
                        WB(work+0x82,0);
                        RAND(r);
                        value=L16C_MUL(F(6.2831855f),L16C_DIV(L16C_CVT_S_W((uint32_t)r>>12&0xFF),F(255.0f)));
                        C(001B1470,value,&value); W(work+0x44,value);
                    }
                }
            }
        } else {
            C(001418F0,self,work);
            if (SH(work+0x70)) {
                rate=F(3.6f);
                if (!L16C_LE(R(work+0x5C),F(20.0f))) {
                    if (!R(work+0x24)) {
                        x=R(0x810360); z=R(0x810368);
                        C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
                    }
                    C(001B1560,self,TARGET,F(0.034906585f),&r);
                    if (r) {
                        C(001B13F0,TARGET,ORIGIN(self),F(40.0f),&r);
                        if (!r) {
                            put4(o,V0,0,F(3.0f),F(50.0f),F(1.0f));
                            C(001B2B10,self,V0,V0); C(001028B8,V0,ORIGIN(self),V0);
                            C(001B3250,self,V0,F(15.0f),&r);
                            if (!r) { WB(self+5,6); WB(self+6,0); }
                        }
                    }
                }
            } else if (SB(work+0x84)) {
                if (!R(work+0x24)) {
                    z=(uint32_t)SH(0x70003B8A);
                    if (!((R(0x70003B68)+z)&0x3F)) {
                        sight_vectors(o,self,work);
                        C(001B13F0,V0,V1,F(15.0f),&r);
                        if (r) WB(work+0x84,0);
                        x=R(work); z=R(work+8);
                        C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
                    }
                }
            } else {
                z=(uint32_t)SH(0x70003B8A);
                if (!((R(0x70003B68)+z)&0x3F)) {
                    sight_vectors(o,self,work);
                    C(001B13F0,V0,V1,F(30.0f),&r);
                    if (!r) WB(work+0x84,1);
                }
            }
        }
        x=R(work+0x44); z=R(self+0xC4);
        C(001B12B0,x,z,L16C_DIV(L16C_MUL(PI,rate),F(180.0f)),&value); W(self+0xC4,value);
        break;
    case 2:
        deg=SH(work+0x70)?F(4.4f):F(2.2f);
        r=SB(work+0x83);
        if (r==2) {
            z=R(self+0xC4); x=R(work+0x44);
            C(001B12B0,x,z,F(0.06981317f),&value); W(self+0xC4,value);
            if (L16C_EQ(value,R(work+0x44))) WB(self+6,1);
        } else {
            if (r) {
                spin=F(-0.3926991f);
                value=L16C_ADD(R(self+0xC4),L16C_DIV(L16C_MUL(PI,deg),F(180.0f)));
            } else {
                spin=F(0.3926991f);
                value=L16C_SUB(R(self+0xC4),L16C_DIV(L16C_MUL(PI,deg),F(180.0f)));
            }
            C(001B1470,value,&value); W(self+0xC4,value);
            C(001029C0,M0); C(00102BB0,M0,M0,R(self+0xC4));
            SUB(ahead(o,self));
            C(001B3250,self,V0,F(20.0f),&r);
            if (!r) {
                C(00102BB0,M0,M0,spin);
                SUB(ahead(o,self));
                C(001B3250,self,V0,F(20.0f),&r);
                if (!r) {
                    WB(self+6,1); W(work+0x44,R(self+0xC4));
                    RAND(r); W(work+0x24,(uint32_t)r>>18&0x3F);
                }
            }
        }
        break;
    default: break;
    }
    x=R(work+0x24); if (x) W(work+0x24,x-1);
    if (B(work+0x30)==9) { RAND(r); CUE(self,l16c_sra((uint32_t)r,18)%5+0x7FE); }
    if (SH(work+0x72)) {
        x=R(work+0x28);
        if (x) W(work+0x28,x-1);
        else { W(work+0x28,0x28); CUE(self,0x803); }
    }
    if (L16C_LE(R(work+0x5C),F(20.0f))) {
        C(0011DF78,L16C_SUB(R(self+0xB4),R(0x810354)),&value);
        if (!L16C_LT(value,F(13.5f))) {
            if (B(self+6)!=2) { RAND(r); WB(self+5,B(0x2458F0+((uint32_t)r>>17&0xF))); }
        } else { RAND(r); WB(self+5,((uint32_t)r>>5&1)?7:8); }
        WB(self+6,0);
        x=B(self+5);
        if (x==7 || x==8) {
            C(0021BE40,PLAYER,self,&r);
            if (r) WB(self+5,2);
            else if (B(self+5)==8 && (B(self+0xD)&0x80)) WB(self+5,9);
        }
        return 0;
    }
    x=R(work+0x20);
    if (x) W(work+0x20,x-1);
    else if (SH(work+0x70) && B(0x81050C)) { RAND(r); W(work+0x20,((uint32_t)r>>12&0xFF)+0x78); }
    else {
        RAND(r);
        WB(self+5,((uint32_t)r>>17&7)?1:3); WB(self+6,0);
    }
    return 0;
}

/* self+5 == 6: clips 3 and 4, then the steering and side probes of 0013DD40;
 * ends at work+0x24 == 0, work+0x70 == 0 or work+0x5C <= 20. */
static uint32_t e0013E6D0(L16C *o, uint32_t self, uint32_t work, uint32_t sp)
{
    uint32_t st=B(self+6),value=0,sign,x,z;
    int32_t r=0;
    switch (st) {
    case 0:
        WB(self+6,st+1); W(work+0x20,0); W(work+0x24,0x1A4); W(work+0x40,F(1.0f));
        WB(work+0x81,1); WB(work+0x82,0); WH(work+0x72,0);
        W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,3,F(5.0f),0);
        break;
    case 1:
        if (R(work+0x30)&0x1000) { WB(self+6,st+1); CLIP(self,4,0,0); }
        break;
    case 2:
        if (e001416D0(o,self,work,sp-0x40)) {
            WB(self+6,B(self+6)+1);
            if (SB(work+0x83)==2) {
                SUB(off_course(o,self,work,&value));
                if (L16C_LE(value,F(0.3926991f))) {
                    r=(int8_t)(SB(work+0x82)+1); WB(work+0x82,(uint32_t)r);
                    if (r>2) W(work+0x24,0);
                }
            }
        } else if ((x=R(work+0x20))!=0) W(work+0x20,x-1);
        else {
            RAND(r); W(work+0x20,(uint32_t)r>>6&0x1F);
            x=R(0x810360); z=R(0x810368);
            C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
        }
        z=R(self+0xC4); x=R(work+0x44);
        C(001B12B0,x,z,F(0.07853982f),&value); W(self+0xC4,value);
        break;
    case 3:
        r=SB(work+0x83);
        if (r==2) {
            z=R(self+0xC4); x=R(work+0x44);
            C(001B12B0,x,z,F(0.07853982f),&value); W(self+0xC4,value);
            if (L16C_EQ(value,R(work+0x44))) WB(self+6,2);
        } else {
            if (r) { sign=F(-0.3926991f); value=L16C_ADD(R(self+0xC4),F(0.061086528f)); }
            else { sign=F(0.3926991f); value=L16C_SUB(R(self+0xC4),F(0.061086528f)); }
            C(001B1470,value,&value); W(self+0xC4,value);
            C(001029C0,M0); C(00102BB0,M0,M0,R(self+0xC4));
            SUB(ahead(o,self));
            C(001B3250,self,V0,F(20.0f),&r);
            if (!r) {
                C(00102BB0,M0,M0,sign);
                SUB(ahead(o,self));
                C(001B3250,self,V0,F(20.0f),&r);
                if (!r) {
                    WB(self+6,2); W(work+0x44,R(self+0xC4));
                    RAND(r); W(work+0x24,((uint32_t)r>>18&0x1F)+0xF);
                }
            }
        }
        break;
    default: break;
    }
    if (B(work+0x30)==9) { RAND(r); CUE(self,l16c_sra((uint32_t)r,17)%5+0x7F9); }
    WH(work+0x72,H(work+0x72)+1);
    x=R(work+0x24); if (x) W(work+0x24,x-1);
    if (!R(work+0x24) || !SH(work+0x70) || L16C_LE(R(work+0x5C),F(20.0f))) {
        WB(self+5,4); WB(self+6,0);
        CUE(self,0x803);
    }
    return 0;
}

/* self+5 == 5: clip 7 until work+0x48 <= 0 with bit 1 of work+0x80, then clip 8. */
static uint32_t e0013EB90(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st,flag;
    WB(work+0x85,1);
    st=B(self+6);
    if (st==0) {
        WB(self+6,st+1); W(work+0x40,F(1.0f)); WB(work+0x81,0); W(work+0x50,F(0.02f));
        WB(work+0x86,B(work+0x86)&1);
        CLIP(self,7,F(5.0f),0);
    } else if (st==1) {
        if (L16C_LT(F(0.6f),R(work+0x4C))) { W(work+0x4C,F(0.6f)); W(work+0x50,0); }
        flag=(uint32_t)SB(work+0x86);
        if (flag==1) {
            WB(work+0x86,flag|2);
            SUB(effect_ahead(o,self,work,F(5.0f)));
        }
        if (L16C_LE(R(work+0x48),0) && (B(work+0x80)&2)) {
            WB(self+6,B(self+6)+1);
            W(work+0x48,0); W(work+0x4C,0); W(work+0x50,0); WB(work+0x81,1);
            CUE(self,0x7F9);
            CLIP(self,8,0,0);
        }
    } else if (st==2) {
        if (R(work+0x30)&0x1000) { WB(self+5,4); WB(self+6,0); WB(work+0x81,0); }
    }
    return 0;
}

/* self+5 == 7: clip 0xC turning to 0x810360; clip 0xD..0xF by the pitch to it
 * (stored at work+0x54); 001EFFD0 once at frame 16 or below. */
static uint32_t e0013EDB0(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),x,z,value=0,pitch;
    int32_t r=0;
    if (st==0) {
        WB(self+6,st+1); W(work+0x20,0); WB(work+0x8D,1); WB(work+0x81,0);
        W(work+0x40,F(1.0f)); W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,0xC,F(5.0f),0);
    } else if (st==1) {
        x=R(0x810360); z=R(0x810368);
        C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
        z=R(self+0xC4); x=R(work+0x44);
        C(001B12B0,x,z,F(0.06981316953897476f),&value); W(self+0xC4,value);
        if (R(work+0x30)&0x1000) {
            WB(self+6,B(self+6)+1);
            C(00102948,V0,R(R(0x275B40)+0x3C)+0xC0);
            C(00102948,V1,TARGET);
            z=R(V1+4); x=R(V0+4);
            z=L16C_ADD(z,F(10.0f)); W(V1+4,z);
            C(0011DF78,L16C_SUB(x,z),&value); W(0x70003A20,value);
            W(V1+4,0); W(V0+4,0);
            C(001B15D0,V0,V1,&value); W(0x70003A24,value);
            x=R(0x70003A20); z=R(0x70003A24);
            C(0011E620,x,z,&value);
            pitch=L16C_MUL(F(-1.0f),value); W(0x70003A20,pitch);
            if (!L16C_LE(pitch,0)) W(0x70003A20,0);
            W(work+0x54,R(0x70003A20));
            pitch=R(0x70003A20);
            if (!L16C_LT(pitch,F(-0.5235988f))) CLIP(self,0xD,0,0);
            else if (!L16C_LT(pitch,F(-1.0471976f))) CLIP(self,0xE,0,0);
            else CLIP(self,0xF,0,0);
        }
    } else if (st==2) {
        if (!R(work+0x20) && L16C_LE(R(self+0x3C),F(16.0f))) {
            W(work+0x20,1);
            C(00102948,V0,ORIGIN(self));
            W(V1,0); W(V1+4,0);
            x=R(V0+4);
            W(V1+8,F(12.0f)); W(V1+0xC,F(1.0f));
            W(V0+4,L16C_ADD(x,F(10.0f)));
            C(001B2B10,self,V1,V1); C(001028B8,V1,V1,V0);
            C(0019A6F0,self,V0,V1,7,0x40,&r);
            if (!r) {
                C(00102948,V0,R(R(0x275B40)+0x3C)+0xC0);
                C(001029C0,M0);
                C(00102B08,M0,M0,R(work+0x54));
                C(00102BB0,M0,M0,R(self+0xC4));
                put4(o,V1,0,0,F(1.0f),F(1.0f));
                C(001026A0,V1,M0,V1);
                C(001EFFD0,(int32_t)0x80000008,V0,V1,(B(self+0xD)&0x80)?0x11:0x10,F(0.7f));
                CUE(self,0x809);
            }
        }
        if (R(work+0x30)&0x1000) { WB(self+5,4); WB(self+6,0); WB(work+0x8D,0); }
    }
    return 0;
}

/* self+5 == 8: clips 0x11, 0x12, 0x13. The decomp C of this function is a near
 * miss; the original instructions are the reference where they differ. */
static uint32_t e0013F240(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),x,z,value=0;
    int32_t r=0;
    switch (st) {
    case 0:
        WB(self+6,st+1); W(work+0x20,0); WB(work+0x8D,1); WB(work+0x81,0x80);
        W(work+0x40,F(1.0f)); W(work+0x48,0);
        WB(work+0x86,B(work+0x86)&1);
        W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,0x11,F(5.0f),0);
        break;
    case 1:
        x=R(0x810360); z=R(0x810368);
        C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
        z=R(self+0xC4); x=R(work+0x44);
        C(001B12B0,x,z,F(0.06981316954f),&value); W(self+0xC4,value);
        if (!(R(work+0x30)&0x1000)) break;
        WB(self+6,B(self+6)+1); W(work+0x48,F(1.5f)); W(work+0x4C,0x3F59999Au);
        CLIP(self,0x12,0,0);
        CUE(self,0x807);
        if (SB(work+0x86)==1) SUB(effect_ahead(o,self,work,0x40200000u));
        SUB(wall_probe(o,self,work,0x41100000u));
        break;
    case 2:
        WB(work+0x85,1); WB(self+0x5E,0);
        if (!R(work+0x20) && !L16C_LT(R(self+0x3C),F(16.0f))) {
            C(0021BE40,PLAYER,self,&r);
            if (!r) {
                C(001A7B80,self,&r);
                if (r) {
                    W(work+0x20,1);
                    /* The speed flag is read before the player's byte is
                     * written back (the near-miss C reads it later). */
                    x=B(PLAYER); z=B(0x81070A);
                    WB(PLAYER,x|2);
                    value=R(0x810360); W(0x810320,L16C_SUB(value,R(self+0xB0)));
                    W(0x810324,0);
                    value=R(0x810368); W(0x810328,L16C_SUB(value,R(self+0xB8)));
                    W(0x8104D4,z?0x41C80000u:0x41A00000u);
                    C(00102760,0x810320,0x810320);
                    RAND(r);
                    C(001B55E0,self,(int32_t)((uint32_t)r>>7&1));
                }
            }
        }
        if (L16C_LT(R(work+0x48),0)) {
            r=SB(work+0x86);
            if (r==1) {
                WB(work+0x86,(uint32_t)r|2);
                SUB(effect_ahead(o,self,work,F(5.0f)));
            }
        }
        if (SB(work+0x80)&2) {
            WB(self+6,B(self+6)+1); WB(work+0x81,1); W(work+0x4C,0);
            CLIP(self,0x13,0,0);
            CUE(self,0x7F9);
        }
        break;
    case 3:
        if (R(work+0x30)&0x1000) {
            WB(self+5,4); WB(self+6,0); WB(work+0x8D,0); WB(work+0x81,0);
            C(001B1470,L16C_ADD(PI,R(self+0xC4)),&value); W(self+0xC4,value);
            CLIP(self,2,0,0);
        }
        break;
    default: break;
    }
    return 0;
}

/* Self's origin = 0x810350 plus (0,0,6.35) rotated by the angle at 0x810374. */
static uint32_t stand_off(L16C *o, uint32_t self)
{
    C(001029C0,M0); C(00102BB0,M0,M0,R(0x810374));
    put4(o,V0,0,0,F(6.35f),F(1.0f));
    C(001026A0,V0,M0,V0); C(001028B8,V0,0x810350,V0);
    C(00102948,ORIGIN(self),V0);
    return 0;
}

/* Effect 0x80000006 at (0.919,0.219,0) in the matrix at (self+0x14C)+0x90. */
static uint32_t bone_effect(L16C *o, uint32_t self)
{
    C(00102958,M0,R(self+0x14C)+0x90);
    put4(o,V0,F(0.919f),F(0.219f),0,F(1.0f));
    C(001026A0,V0,M0,V0);
    C(001EFD90,(int32_t)0x80000006,V0,0x810370);
    return 0;
}

/* self+5 == 9: nine sub-states through the table at 0x26D240 (a switch here). */
static uint32_t e0013F770(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),x,z,value=0,t,flag;
    int32_t r=0;
    switch (st) {
    case 0:
        WB(self+6,B(self+6)+1); W(work+0x20,0); WB(work+0x8D,1); WB(work+0x81,0);
        W(work+0x40,F(1.0f)); W(work+0x48,0);
        WB(work+0x86,B(work+0x86)&1);
        W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,0x14,F(5.0f),0);
        CUE(self,0x808);
        break;
    case 1:
        x=R(0x810360); z=R(0x810368);
        C(001B1240,ORIGIN(self),x,z,&value); W(work+0x44,value);
        z=R(self+0xC4); x=R(work+0x44);
        C(001B12B0,x,z,F(0.06981317f),&value); W(self+0xC4,value);
        if (R(work+0x30)&0x1000) {
            WB(self+6,B(self+6)+1); WB(work+0x81,1);
            CLIP(self,0x15,0,0);
        }
        break;
    case 2:
        if (R(work+0x30)&0x1000) {
            WB(self+6,st+1); WB(work+0x81,0x80); W(work+0x48,F(1.5f)); W(work+0x4C,F(0.85f));
            CLIP(self,0x16,0,0);
            CUE(self,0x80B);
            if (SB(work+0x86)==1) SUB(effect_ahead(o,self,work,F(2.5f)));
            SUB(wall_probe(o,self,work,F(9.0f)));
        }
        break;
    case 3:
        WB(work+0x85,1);
        C(0021BED0,PLAYER,&r);
        if (!r) C(001A7B80,self,&r); else r=0;
        if (r) {
            WB(self+6,B(self+6)+1);
            WB(PLAYER,B(PLAYER)|2);
            SUB(bone_effect(o,self));
            W(0x70003A20,B(0x81070A)?F(30.0f):F(25.0f));
            value=R(0x8104D0); t=R(0x70003A20);
            if (L16C_LE(value,t)) {
                WB(self+6,8); W(0x8104D4,R(0x8104D0));
                CLIP(self,0x1A,0,0);
                break;
            }
            W(0x8104D4,t);
            C(0021BF90,PLAYER,self);
            WB(work+0x89,1);
            C(001B1470,L16C_ADD(PI,R(self+0xC4)),&value); W(0x810374,value);
            CLIP(self,0x17,0,0);
            SUB(stand_off(o,self));
            WB(work+0x81,0x82);
            break;
        }
        if (L16C_LT(R(work+0x48),0) || (SB(work+0x80)&2)) {
            WB(self+6,8);
            CLIP(self,0x1A,0,0);
        }
        break;
    case 4:
        WB(work+0x85,1);
        SUB(stand_off(o,self));
        W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        if (R(work+0x30)&0x1000) {
            WB(self+6,B(self+6)+1); W(work+0x20,0); W(work+0x24,0);
            CLIP(self,0x18,0,0);
        }
        break;
    case 5:
        WB(work+0x85,1);
        SUB(stand_off(o,self));
        W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
        if (B(0x8106BD)==1) {
            WB(self+6,6); WB(work+0x89,0); WB(0x8106BD,0); WB(work+0x81,0x80);
            W(work+0x48,F(1.5f)); W(work+0x4C,F(-0.45f));
            CLIP(self,0x1C,0,0);
            CUE(self,0x80D);
            break;
        }
        x=R(work+0x20)+1; W(work+0x20,x);
        if (x>=0xB4) {
            W(work+0x20,0); W(work+0x24,0);
            WB(PLAYER,B(PLAYER)|2);
            CUE(self,0x80A);
            SUB(bone_effect(o,self));
            W(0x70003A20,B(0x81070A)?F(15.0f):F(12.0f));
            value=R(0x8104D0); t=R(0x70003A20);
            if (L16C_LE(value,t)) {
                WB(self+6,6); WB(work+0x89,0); WB(0x8106BD,0); WB(work+0x81,0x80);
                W(work+0x48,F(1.3f)); W(work+0x4C,F(-0.2f));
                W(0x8104D4,R(0x8104D0));
                CLIP(self,0x1C,0,0);
                break;
            }
            W(0x8104D4,t);
            CLIP(self,0x19,0,0);
        }
        x=R(work+0x24)+1; W(work+0x24,x);
        if (x>=0x82) {
            RAND(r); W(work+0x24,(uint32_t)r>>19&0x3F);
            CUE(self,0x808);
        }
        break;
    case 6:
        WB(work+0x85,1);
        if (L16C_LE(R(work+0x48),0)) {
            flag=(uint32_t)SB(work+0x86);
            if (flag==1) {
                WB(work+0x86,flag|2);
                SUB(effect_ahead(o,self,work,F(5.0f)));
            }
            if (SB(work+0x80)&2) {
                WB(self+6,B(self+6)+1); WB(work+0x81,1);
                W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
                CLIP(self,0x1B,0,0);
            }
        } else if (L16C_LT(R(work+0x4C),0)) {
            W(V0,0); W(V1,0); W(V0+4,F(3.0f)); W(V1+4,F(3.0f));
            W(V0+8,F(4.0f)); W(V0+0xC,F(1.0f)); W(V1+8,F(-4.0f)); W(V1+0xC,F(1.0f));
            C(001B2B10,self,V0,V0); C(001B2B10,self,V1,V1);
            C(001028B8,V0,ORIGIN(self),V0); C(001028B8,V1,ORIGIN(self),V1);
            C(001B3390,self,V0,V1,F(0.7853982f),&r);
            if (r) W(work+0x4C,0);
        }
        break;
    case 7:
        if (R(work+0x30)&0x1000) {
            WB(self+5,4); WB(self+6,0); WB(work+0x81,0); WB(work+0x85,0); WB(work+0x8D,0);
        }
        break;
    case 8:
        WB(work+0x85,1);
        if (L16C_LE(R(work+0x48),0)) {
            flag=(uint32_t)SB(work+0x86);
            if (flag==1) {
                WB(work+0x86,flag|2);
                SUB(effect_ahead(o,self,work,F(5.0f)));
            }
            if (SB(work+0x80)&2) {
                WB(self+6,7); WB(work+0x81,0x81);
                W(work+0x48,0); W(work+0x50,0); W(work+0x4C,0);
                CLIP(self,8,0,0);
                CUE(self,0x7F9);
            }
        }
        break;
    default: break;
    }
    return 0;
}

/* self+5 == 10: clips 0x26..0x28, then self+0x60 grows by 0.001 up to 1.1. */
static uint32_t e00140440(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6);
    switch (st) {
    case 0:
        WB(self+6,st+1); WB(work+0x81,0); W(work+0x40,F(1.0f)); W(work+0x50,0); W(work+0x4C,0);
        CLIP(self,0x26,F(5.0f),0);
        break;
    case 1:
        if (R(work+0x30)&0x1000) {
            WB(self+6,st+1);
            CLIP(self,0x27,F(5.0f),0);
            CUE(self,0x812);
        }
        break;
    case 2:
        if (R(work+0x30)&0x1000) {
            int32_t ignored=0;
            WB(self+6,st+1); WB(work+0x88,0x64);
            C(001EFE00,(int32_t)0x8000002B,self,&ignored);
            CLIP(self,0x28,F(5.0f),0);
            CUE(self,0x813);
        }
        break;
    case 3:
        WB(work+0x88,0x64);
        if ((R(work+0x30)&0x1000) && L16C_EQ(R(self+0x60),F(1.1f))) {
            WB(self+5,4); WB(self+6,0); WB(self+0xD,B(self+0xD)|0x80);
            WH(self+0x34,B(0x81070A)?0xC8:0x64);
            C(001C63E0,self,0);
            CUE(self,0x815);
        }
        put4(o,V0,0x3A83126Fu,0x3A83126Fu,0x3A83126Fu,0);
        C(001028B8,self+0x60,V0,self+0x60);
        if (!L16C_LE(R(self+0x60),F(1.1f))) {
            W(self+0x60,F(1.1f)); W(self+0x64,0x3F8CCCCDu); W(self+0x68,0x3F8CCCCDu); W(self+0x6C,F(1.0f));
        }
        break;
    default: break;
    }
    return 0;
}

/* With work+0x89 set: clears it, 0021C040, and clears 0x8106BD if it is 1. */
static uint32_t release_grab(L16C *o, uint32_t self, uint32_t work)
{
    if (SB(work+0x89)) {
        WB(work+0x89,0);
        C(0021C040,PLAYER,self);
        if (B(0x8106BD)==1) WB(0x8106BD,0);
    }
    return 0;
}

/* The record reset both 00140760 and 001408D0 end in: self+5 = 8. */
static void reset_to_8(L16C *o, uint32_t self, uint32_t work)
{
    WB(self+4,1); WB(self+5,8); WB(self+6,0); WB(self,1);
    WH(self+0x36,0); WB(work+0x88,0); WB(work+0x8D,0); WH(work+0x70,0xF0);
}

/* 00140690's self+5 == 0: clip 0x21, or one of the table at 0x2753C0. */
static uint32_t e00140760(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6);
    int32_t r=0,clip;
    if (st==0) {
        WB(self+6,st+1); W(work+0x40,F(1.0f)); WB(work+0x81,0); W(work+0x50,0); W(work+0x4C,0);
        if (SH(self+0x36)&0x8000) {
            CUE(self,0x80F);
            CLIP(self,0x21,F(1.0f),0);
        } else {
            CUE(self,0x80E);
            RAND(r);
            clip=SH(0x2753C0+(uint32_t)(l16c_sra((uint32_t)r,3)%3*2));
            CLIP(self,clip,F(1.0f),0);
        }
        SUB(release_grab(o,self,work));
    } else if (st==1) {
        if (R(work+0x30)&0x1000) reset_to_8(o,self,work);
    }
    return 0;
}

/* 00140690's self+5 == 1: clip 0x22, yaw from self+0x70/0x78, then clip 0x24. */
static uint32_t e001408D0(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6),value=0,x,z;
    if (st==0) {
        WB(self+6,st+1); WB(work+0x81,0); W(work+0x40,F(1.0f));
        W(work+0x48,0x3FC00000u); W(work+0x4C,0xBF800000u); W(work+0x50,0);
        CLIP(self,0x22,0,0);
        CUE(self,0x80E);
        z=R(self+0x78); x=R(self+0x70);
        C(0011E620,x,z,&value);
        C(001B1470,L16C_ADD(PI,value),&value); W(self+0xC4,value);
        SUB(release_grab(o,self,work));
    } else if (st==1) {
        if (SB(work+0x80)&2) W(work+0x4C,0);
        if (R(work+0x30)&0x1000) {
            if (SH(self+0x34)) { WB(self+6,B(self+6)+1); CLIP(self,0x24,F(1.0f),0); }
            else { WB(self+5,2); WB(self+6,0); WB(work+0x8C,1); }
        }
    } else if (st==2) {
        if (R(work+0x30)&0x1000) reset_to_8(o,self,work);
    }
    return 0;
}

/* 00140690's self+5 == 2: clip 0x25 or 0x23, then cue 0x810 and 001EFE00. */
static uint32_t e00140A90(L16C *o, uint32_t self, uint32_t work)
{
    uint32_t st=B(self+6);
    int32_t r=0;
    if (st==0) {
        WB(self+6,st+1);
        C(001B1190,(int32_t)B(self+0x9A));
        W(work+0x50,0); W(work+0x4C,0);
        if (SB(work+0x8C)) { WB(work+0x81,0); CLIP(self,0x25,F(1.0f),0); }
        else { WB(work+0x81,1); CLIP(self,0x23,F(1.0f),0); }
        CUE(self,0x811);
        SUB(release_grab(o,self,work));
    } else if (st==1) {
        if (R(work+0x30)&0x1000) {
            WB(self+6,st+1); WB(work+0x8E,0); WB(work+0x81,0);
            CUE(self,0x810);
            C(001EFE00,(int32_t)0x8000001E,self,&r);
            if (!r) WB(self+4,3);
        }
    }
    return 0;
}

#define ENTRY(addr) \
int em_level16_creature_##addr(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, EmLevel16CreatureFault *fault) \
{ \
    L16C o; \
    if (l16c_begin(&o, h, fault)) return -1; \
    (void)e##addr(&o, a0, a1); \
    return l16c_end(&o); \
}
ENTRY(0013D980)
ENTRY(0013DB70)
ENTRY(0013DC60)
ENTRY(0013EB90)
ENTRY(0013EDB0)
ENTRY(0013F240)
ENTRY(0013F770)
ENTRY(00140440)
ENTRY(00140760)
ENTRY(001408D0)
ENTRY(00140A90)

int em_level16_creature_0013DD40(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16CreatureFault *fault)
{
    L16C o;
    if (l16c_begin(&o, h, fault)) return -1;
    (void)e0013DD40(&o, a0, a1, sp);
    return l16c_end(&o);
}

int em_level16_creature_0013E6D0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, EmLevel16CreatureFault *fault)
{
    L16C o;
    if (l16c_begin(&o, h, fault)) return -1;
    (void)e0013E6D0(&o, a0, a1, sp);
    return l16c_end(&o);
}

int em_level16_creature_001416D0(const EmLevel16CreatureHooks *h, uint32_t a0, uint32_t a1, uint32_t sp, int32_t *result, EmLevel16CreatureFault *fault)
{
    L16C o;
    if (!result || l16c_begin(&o, h, fault)) return -1;
    uint32_t value = e001416D0(&o, a0, a1, sp);
    if (l16c_failed(&o)) return -1;
    *result = (int32_t)value;
    return 0;
}
