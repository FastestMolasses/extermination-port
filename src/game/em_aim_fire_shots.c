/* Native equipment one-shots, original addresses in em_aim_fire_shots.h.
 * Storage and workers are supplied by the live binder. */
#include "game/em_aim_fire_shots.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { const EmAimFireShots *bus; int failed; } Run;
static uint32_t get(Run *s,uint32_t a,unsigned n)
{
    if (s->failed) return 0;
    const uint8_t *p=s->bus->map(s->bus->context,a,n,0);
    if (!p) { s->failed=1; return 0; }
    uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(i*8);return v;
}
static void put(Run *s,uint32_t a,unsigned n,uint32_t v)
{
    if(s->failed)return;
    uint8_t *p=s->bus->map(s->bus->context,a,n,1);
    if(!p) { s->failed=1;return; }
    for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(i*8));
}
static uint32_t call(Run *s,uint32_t entry,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3,uint32_t a4,uint32_t a5,uint32_t f12,int fp)
{
    EmAimFireShotsCall c={{a0,a1,a2,a3,a4,a5},{f12,0,0,0},0,0};
    if(!s->failed && s->bus->call(s->bus->context,entry,&c)<0)s->failed=1;
    return fp?c.f0:c.v0;
}
#define G(a) get(s,(a),4)
#define B(a) get(s,(a),1)
#define W(a,v) put(s,(a),4,(v))
#define H(a,v) put(s,(a),2,(v))
#define U(a,v) put(s,(a),1,(v))
#define C0(e) call(s,e,0,0,0,0,0,0,0,0)
#define C1(e,a) call(s,e,a,0,0,0,0,0,0,0)
#define C2(e,a,b) call(s,e,a,b,0,0,0,0,0,0)
#define C3(e,a,b,c) call(s,e,a,b,c,0,0,0,0,0)
#define C4(e,a,b,c,d) call(s,e,a,b,c,d,0,0,0,0)
#define C6(e,a,b,c,d,f,g) call(s,e,a,b,c,d,f,g,0,0)
#define CF(e,a,b,f) call(s,e,a,b,0,0,0,0,f,0)
#define RF(e,f) call(s,e,0,0,0,0,0,0,f,1)
#define RF2(e,a,b) call(s,e,a,b,0,0,0,0,0,1)
#define CHECK() do { if(s->failed)return -1; } while(0)
#define ONE UINT32_C(0x3F800000)
#define A UINT32_C(0x700038A0)
#define N UINT32_C(0x700038E0)
#define FLAG UINT32_C(0x700031E8)
static int surface(Run *s,uint32_t a,uint32_t b,int32_t *result)
{
    uint32_t hit=C2(0x19B6C0,a,b);CHECK();
    if(!hit){*result=0;return 0;}
    C2(0x1031E0,0x70003620,0x700031B0);CHECK();
    uint32_t record=G(0x700031D0);
    W(0x70003630,G(record+0x24));W(0x70003634,G(record+0x28));W(0x70003638,G(record+0x2C));W(0x7000363C,ONE);
    uint32_t type=B(record+0x1A);CHECK();
    if(type==0x5A)C3(0x1EFD90,0x8000002C,0x70003620,0x70003630);
    else if(type==0x5B) {
        C3(0x1EFD90,0x80000026,0x70003620,0x70003630);CHECK();
        CF(0x1E8B90,0x70003620,0,ONE);
    } else if(type==0x5C)C3(0x1EFD90,0x80000067,0x70003620,0x70003630);
    CHECK();*result=1;return 0;
}
static int flash(Run *s,uint32_t actor)
{
    uint32_t r=C0(0x15D2F0);CHECK();
    int crouch=r==2 || r==0x82;
    uint32_t mode=B(0x810525)==3?(crouch?4:1):(crouch?3:0);
    uint32_t fx=C1(0x1F4F40,mode);CHECK();
    if(!fx)return 0;
    C2(0x102948,fx+0xB0,actor+0xB0);CHECK();
    C2(0x102958,fx+0xD0,G(G(0x275B40))+0x90);CHECK();
    W(A,0x3E4CCCCD);W(A+4,0x3E4CCCCD);W(A+8,0);W(A+12,0);
    C3(0x1026A0,A+16,G(G(0x275B40))+0x90,A);CHECK();
    C3(0x1028B8,fx+0x100,actor+0xB0,A+16);CHECK();
    W(fx+0x10C,ONE);CHECK();return 0;
}
static int projectile(Run *s,uint32_t actor,int grenade,int32_t *result)
{
    uint32_t p=C1(0x1AFA90,1);CHECK();
    if(p) {
        U(p+3,3);W(p+0x10,grenade?0x18B3E0:0x18AF50);
        if(grenade)U(p+0xD,B(0x81070B));
        W(p+0xB0,G(actor+0xB0));W(p+0xB4,G(actor+0xB4));W(p+0xB8,G(actor+0xB8));
        uint32_t offset=grenade?0x70:0xC0;
        W(p+offset,G(actor+0xC0));W(p+offset+4,G(actor+0xC4));W(p+offset+8,G(actor+0xC8));
        if(!grenade)W(p+0x24,G(0x8104E0)==12 && B(0x810CA4)==1?G(0x8106E0):0);
    }
    CHECK();*result=0;return 0;
}
/* Shared surface classification. An effect cue retains the current reaction. */
static int classify(Run *s,uint32_t type,uint32_t point,int reaction,int ordinary)
{
    if(ordinary || reaction==0x101) {
        if(type-2u<3)reaction=0x201;
        else if(type==5 || type==8) {
            C3(0x1EFD90,type==5?0x8000002C:0x80000067,point,N);
            W(FLAG,UINT32_MAX);
        } else if(ordinary)reaction=0x101;
    } else if(reaction==0x300) {
        C3(0x1EFD90,0x80000067,point,N);W(FLAG,UINT32_MAX);
    }
    return reaction;
}
static int bullet(Run *s,uint32_t actor,int32_t *result)
{
    uint32_t target=0;int reaction=0,locked=0;
    uint32_t mode=G(0x8104E0);
    if((mode==12 || mode==0x29) && (B(0x810CA4)==1 || B(0x810CA4)==0)) {
        if(B(0x810CA4)==1)target=G(0x8106E0);
        else {
            uint32_t selection=B(0x8102B0+0x2F0);
            if(selection==2)target=G(0x8106E8);
            else if(selection==1)target=G(0x8106E4);
            if(!target)target=G(0x8106E0);
        }
        if(target){C2(0x183C40,target,A);CHECK();locked=1;}
    }
    if(!locked) {
        CF(0x103230,A,actor+0xC0,0x43820000);CHECK();
        C3(0x1028B8,A,A,actor+0xA0);CHECK();W(A+12,ONE);
        C3(0x1028D0,A+0x30,A,actor+0xA0);CHECK();W(A+0x3C,ONE);
    } else {
        C3(0x1028D0,A+0x30,A,actor+0xA0);CHECK();W(A+0x3C,ONE);
        C2(0x102760,N,A+0x30);CHECK();CF(0x102900,N,N,0x40A00000);CHECK();
        C3(0x1028B8,A,A,N);CHECK();W(A+12,ONE);
    }
    C2(0x102948,A+0x50,A);CHECK();W(FLAG,0);
    uint32_t hit=C4(0x19A570,actor+0xA0,A,7,0x20);CHECK();
    if(!hit){C2(0x1860A0,actor+0xA0,A+0x50);CHECK();*result=0;return 0;}
    C2(0x1031E0,A,0x700031B0);CHECK();W(A+12,ONE);
    uint32_t primary=G(0x700031D0),secondary=G(0x700031D4);
    W(N,G(primary+0x24));W(N+4,G(primary+0x28));W(N+8,G(primary+0x2C));W(N+12,ONE);
    uint32_t type=B(primary+0x1A),parm=G(primary+0x1C);
    if(C2(0x1860A0,actor+0xA0,A))W(FLAG,UINT32_MAX);CHECK();
    if(hit==1) {
        if(secondary && B(secondary)==1 && (B(secondary+2)&0x1F)==2) {
            if(C6(0x1B41F0,secondary,A,actor+0xC0,parm,0,5))W(FLAG,UINT32_MAX);CHECK();
        } else if(secondary) reaction=0x100;
    } else {
        if(target && B(target) && (B(target+2)&0x1F)==2) {
            C2(0x183C40,target,A+0x20);CHECK();
            C3(0x1028D0,A+0x10,A+0x20,actor+0xB0);CHECK();W(A+0x1C,ONE);
            C3(0x102718,A+0x10,A+0x30,A+0x10);CHECK();
            uint32_t mag0=RF2(0x102738,A+0x30,A+0x30);CHECK();mag0=RF(0x11E748,mag0);CHECK();W(0x70003A20,mag0);
            uint32_t mag1=RF2(0x102738,A+0x10,A+0x10);CHECK();mag1=RF(0x11E748,mag1);CHECK();W(0x70003A24,mag1);
            if(em_ee_c_lt_bits(em_ee_div_bits(G(0x70003A24),G(0x70003A20)),0x40900000)) {
                if(B(target)==1 && C6(0x1B41F0,target,A+0x20,actor+0xC0,parm,0,5))W(FLAG,UINT32_MAX);CHECK();
                hit=1;C2(0x1031E0,A,A+0x20);CHECK();W(A+12,ONE);
            }
        }
        if(hit!=1) {
            if(hit==2 && secondary) {
                if(B(secondary)) {
                reaction=(int16_t)C1(0x1839A0,secondary);CHECK();
                reaction=classify(s,type,A,reaction,0);CHECK();
                H(secondary+0x36,5);C2(0x102948,secondary+0x70,actor+0xC0);CHECK();
                }
            } else {reaction=classify(s,type,A,reaction,1);CHECK();}
        }
    }
    uint32_t slot=C1(0x1AFA90,1);CHECK();
    if(!slot){*result=0;return 0;}
    if(G(FLAG)==UINT32_MAX)U(slot+0xD,0);
    else {U(slot+0xD,1);H(slot+0x2E,(uint32_t)reaction);}
    U(slot+3,3);W(slot+0xB0,G(A));W(slot+0xB4,G(A+4));W(slot+0xB8,G(A+8));W(slot+0x10,0x18ABA0);
    if(hit!=1)C2(0x102948,slot+0xC0,N);else W(slot+0xCC,0);
    CHECK();*result=1;return 0;
}
static int shotgun(Run *s,uint32_t actor,int32_t *result)
{
    CF(0x103230,A,actor+0xC0,0x42C80000);CHECK();C3(0x1028B8,A,A,actor+0xA0);CHECK();W(A+12,ONE);
    C2(0x102948,0x70003900,actor+0xA0);CHECK();W(0x7000390C,ONE);
    uint32_t hit=C4(0x19A570,0x70003900,A,7,0x20);CHECK();
    if(!hit){C2(0x1860A0,0x70003900,A);CHECK();*result=0;return 0;}
    C3(0x1028D0,A+16,0x700031B0,actor+0xA0);CHECK();
    uint32_t value=RF2(0x102738,A+16,A+16);CHECK();value=RF(0x11E748,value);CHECK();W(0x70003A20,value);
    if(em_ee_c_lt_bits(value,0x428C0000))W(0x70003A20,0x428C0000);
    W(0x70003A38,em_ee_mul_bits(0x41A00000,em_ee_div_bits(G(0x70003A20),0x42A00000)));
    C2(0x1CD390,0x700037A0,actor+0xC0);CHECK();C3(0x102918,0x700037A0,0x700037A0,A);CHECK();
    value=C0(0x122BB8);CHECK();W(0x70003A3C,em_ee_mul_bits(0x30000000,em_ee_cvt_s_w_bits(value)));
    for(unsigned i=0;i<10;i++) {
        if(!i){C2(0x102948,A+16,A);CHECK();}
        else {
            uint32_t spread=G(0x70003A38),minr=0x40800000;
            if(em_ee_c_le_bits(0x41A00000,spread))spread=0x41A00000;
            W(0x70003A24,spread);if(!em_ee_c_le_bits(minr,spread))minr=spread;W(0x70003A28,minr);
            value=C0(0x122BB8);CHECK();
            W(0x70003A2C,em_ee_add_bits(minr,em_ee_mul_bits(em_ee_sub_bits(spread,minr),em_ee_mul_bits(0x30000000,em_ee_cvt_s_w_bits(value)))));
            value=RF(0x1B1470,em_ee_add_bits(G(0x70003A3C),em_ee_mul_bits(0x3F20D97C,em_ee_cvt_s_w_bits(i))));CHECK();W(0x70003A20,value);
            value=RF(0x11DE90,G(0x70003A20));CHECK();W(A+16,em_ee_mul_bits(G(0x70003A2C),value));
            value=RF(0x11E2A8,G(0x70003A20));CHECK();W(A+20,em_ee_mul_bits(G(0x70003A2C),value));
            W(A+24,0);W(A+28,ONE);C3(0x1026A0,A+16,0x700037A0,A+16);CHECK();W(A+28,ONE);
        }
        hit=C4(0x19A570,0x70003900,A+16,7,0x20);CHECK();if(!hit)continue;
        C3(0x1028D0,A+32,0x700031B0,actor+0xB0);CHECK();value=RF2(0x102738,A+32,A+32);CHECK();value=RF(0x11E748,value);CHECK();W(0x70003A20,value);
        C2(0x102948,A+32,0x700031B0);CHECK();W(A+44,ONE);
        uint32_t src=G(0x700031D0);W(N,G(src+0x24));W(N+4,G(src+0x28));W(N+8,G(src+0x2C));W(N+12,i?0:ONE);
        value=G(0x70003A20);uint32_t damage=em_ee_c_lt_bits(value,0x41A00000)?0x2A:em_ee_c_lt_bits(value,0x42480000)?0x23:0x19;
        uint32_t target=G(0x700031D4),type=B(src+0x1A),parm=G(src+0x1C);int reaction=0;
        W(FLAG,0);if(C2(0x1860A0,0x70003900,A+32))W(FLAG,UINT32_MAX);CHECK();
        if(hit==1) {
            if(target) {
                if(B(target)==1 && (B(target+2)&0x1F)==2) {C6(0x1B41F0,target,A+32,actor+0xC0,parm,0,damage);CHECK();W(FLAG,UINT32_MAX);}
                else reaction=0x100;
            }
        } else if(hit==2 && target) {
            if(B(target)) {
                reaction=(int16_t)C1(0x1839A0,target);CHECK();reaction=classify(s,type,A+32,reaction,0);CHECK();
                H(target+0x36,damage);C2(0x102948,target+0x70,actor+0xC0);CHECK();
            }
        } else {reaction=classify(s,type,A+32,reaction,1);CHECK();}
        uint32_t slot=C1(0x1AFA90,1);CHECK();if(!slot)continue;
        if(G(FLAG)==UINT32_MAX)U(slot+13,0);
        else {U(slot+13,i?2:1);H(slot+0x2E,(uint32_t)reaction);}
        U(slot+3,3);W(slot+0xB0,G(A+32));W(slot+0xB4,G(A+36));W(slot+0xB8,G(A+40));W(slot+0x10,0x18ABA0);
        if(hit!=1){W(N+12,ONE);C2(0x102948,slot+0xC0,N);CHECK();}else W(slot+0xCC,0);
    }
    CHECK();*result=0;return 0;
}
int em_aim_fire_shots_run(const EmAimFireShots *bus,uint32_t entry,uint32_t actor,uint32_t argument,int32_t *result)
{
    if(!bus || !bus->map || !bus->call || !result)return -1;
    Run state={bus,0};Run *s=&state;
    switch(entry) {
    case 0x1860A0:return surface(s,actor,argument,result);
    case 0x1861C0:return bullet(s,actor,result);
    case 0x1869A0:return projectile(s,actor,0,result);
    case 0x186A60:return shotgun(s,actor,result);
    case 0x1872C0:return projectile(s,actor,1,result);
    case 0x187CC0:return flash(s,actor);
    default:return -1;
    }
}
