/* Level-15 boot support, translated from the decomp C. UNWIRED.
 * Internal translated calls execute here; external workers stay explicit.
 * Original table/record bytes are borrowed through the memory contract. */
#include "em_level15_support_internal.h"
#define B(a) l15s_u8(o,(a))
#define H(a) l15s_u16(o,(a))
#define SH(a) l15s_s16(o,(a))
#define R(a) l15s_u32(o,(a))
#define W(a,v) l15s_w32(o,(a),(v))
#define WB(a,v) l15s_w8(o,(a),(v))
#define WH(a,v) l15s_w16(o,(a),(v))
#define F(v) l15s_bits(v)
#define C(fn,...) do { if (l15s_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

static uint32_t e001418F0(L15S *o, uint32_t self, uint32_t work)
{
    int32_t timer, r=0;
    uint32_t p,x,z,angle=0;
    if (SH(work+0x70)) {
        if (L15S_LE(R(work+0x5C),F(70.0f))) WH(work+0x78,0);
        else {
            timer=(int16_t)(SH(work+0x78)+1); WH(work+0x78,(uint32_t)timer);
            if (timer>=180) { WH(work+0x70,0); WH(work+0x78,0); }
        }
        return 0;
    }
    if (B(self+0xA)&1) { WH(work+0x70,240); WH(work+0x78,0); return 0; }
    p=R(R(0x275B40)+0x3C); z=R(p+0xB8); x=R(p+0xB0);
    C(0011E620,x,z,&angle); C(001B1470,L15S_SUB(angle,F(1.5707964f)),&angle);
    C(001B3F10,self,angle,F(18.0f),&r);
    if (r) {
        timer=(int16_t)(SH(work+0x78)+1); WH(work+0x78,(uint32_t)timer);
        if (timer>=(int32_t)B(0x2753C8+(B(0x81050C)&3))) { WH(work+0x70,240); WH(work+0x78,0); }
    } else WH(work+0x78,0);
    return 0;
}

static uint32_t e00153EA0(L15S *o, uint32_t self)
{
    C(001B1190,(int32_t)B(self+0x9A)); C(001AFC10,self); return 0;
}

static uint32_t e00185420(L15S *o, int32_t event)
{
    int32_t n=SH(0x275B64), r=0;
    uint32_t next=R(0x275B5C), obj;
    while (n && !l15s_failed(o)) {
        obj=R(next); n=(int32_t)((uint32_t)n-1); next+=4;
        if ((B(obj)&1) && (B(obj+2)&128) && !B(obj+0xB)) {
            C(00184D20,0x8102B0,obj,event,&r); if (r) return obj;
        }
    }
    return 0;
}

static uint32_t e001A91C0(L15S *o, uint32_t self, uint32_t out, uint32_t time)
{
    uint32_t kind=B(self+0xD), value;
    int32_t mode;
    switch (kind) {
    case 0: case 1: case 2: case 3: case 4: value=60; break;
    case 5: value=100; break;
    case 6: value=L15S_LT(time,F(10.0f))?80:30; break;
    case 7:
        mode=SH(self+0x94);
        if (L15S_LT(time,F(10.0f))) value=mode==0?280:mode==1?180:380;
        else value=mode==0?150:mode==1?120:180;
        break;
    case 8: case 9: case 12: case 13: value=5; break;
    case 10: value=40; break;
    case 11: case 14: return 0;
    default: WH(out+0x36,H(out+0x36)|0x2000); return 0;
    }
    WH(out+0x36,value); WH(out+0x36,H(out+0x36)|0x2000); return 0;
}

static uint32_t e001BEB70(L15S *o, uint32_t self)
{
    uint32_t child=0,count=0;
    for (uint32_t i=0;i<3 && !l15s_failed(o);++i) {
        C(001AFA90,8,&child);
        if (!child) continue;
        C(00102948,child+0xB0,self+0xB0); C(00102948,child+0xA0,self+0xB0);
        W(child+0xB4,L15S_SUB(R(child+0xB4),F(6.0f)));
        W(child+0xA4,L15S_SUB(R(child+0xA4),F(6.0f)));
        C(00102948,child+0xC0,self+0xC0);
        WB(child+3,0x11); WB(child+0xD,i); W(child+0x10,0x1BF3C0); ++count; W(child+0x20,self);
    }
    return count;
}

static uint32_t e001BEC40(L15S *o, uint32_t self, uint32_t work, int32_t state)
{
    int32_t mode=(int16_t)state;
    if ((H(work)&0x1000) && SH(work+2)!=mode) {
        if (!mode) { C(001D0D40,self,0x24E960,0x65,1); WH(work+2,0); }
        else { C(001D0D40,self,0x24F470,0x29,1); WH(work+2,1); }
    }
    return 0;
}

static uint32_t e001C2690(L15S *o, uint32_t self, uint32_t a, uint32_t b)
{
    uint32_t x,z,angle=0;
    C(001029C0,0x70003440);
    z=R(self+0x78); x=R(self+0x70); C(0011E620,x,z,&angle); C(001B1470,angle,&angle);
    C(00102BB0,0x70003440,0x70003440,angle);
    C(001026A0,0x700038C0,0x70003440,a);
    C(001028B8,0x700038C0,0x700038C0,self+0xB0);
    C(001026A0,0x700038D0,0x70003440,b);
    C(0019B4C0,self,0x700038C0,0x700038D0,0x80000006); return 0;
}

static uint32_t e001D6580(L15S *o, int32_t slot, uint32_t a, uint32_t b, int32_t mode)
{
    uint32_t base=R(0x275670)+(uint32_t)slot*4, cursor, body;
    uint8_t zero[16]={0};
    WB(R(base+0x10)+3,0x10); W(R(base+0x10)+4,0); WH(R(base+0x10),10);
    cursor=R(base+0x10); body=cursor+0x10; W(base+0x10,cursor+0xB0);
    l15s_wq(o,body,zero); W(cursor+0x1C,0x50000009);
    l15s_w64(o,cursor+0x20,((uint64_t)(uint32_t)(mode|4)<<47)|UINT64_C(0x8000400000008001));
    l15s_w64(o,cursor+0x28,0x41414141);
    for (uint32_t i=0;i<4;++i) {
        C(00102948,body+0x20+i*0x20,b+i*0x10);
        C(00102948,body+0x30+i*0x20,a+i*0x10);
    }
    return body;
}

static void append_ref(L15S *o, uint32_t base, uint32_t pointer, uint32_t count)
{
    WB(R(base+0x10)+3,0x30); W(R(base+0x10)+4,pointer);
    WH(R(base+0x10),count); W(base+0x10,R(base+0x10)+0x10);
}

static uint32_t e001D4440(L15S *o, int32_t slot, uint32_t mesh, uint32_t color)
{
    int32_t r=0;
    uint32_t bank, base, offset, block, count;
    C(001D2090,slot,0x23D0C0);
    bank=R(0x275680); C(001D6930,slot,(int32_t)bank,8,8,0x26E4C0);
    C(001D1F80,slot,0,2);
    C(001281C0,L15S_MUL(F(128.0f),R(color)),&r); W(0x251C20,(uint32_t)r);
    C(001281C0,L15S_MUL(F(128.0f),R(color+4)),&r); W(0x251C24,(uint32_t)r);
    C(001281C0,L15S_MUL(F(128.0f),R(color+8)),&r); W(0x251C28,(uint32_t)r);
    (void)e001D6580(o,slot,0x26E4D0,0x251C10,0x48);
    C(001D1F20,slot); C(001D1F80,slot,1,7);
    bank=R(0x275680); C(001D6BA0,slot,(int32_t)bank,8,8,0,0); C(001D1FF0,slot,3);
    base=R(0x275670); offset=R(base+0x9C)<<7;
    base+=(uint32_t)slot*4; block=0x817040+offset;
    append_ref(o,base,block,8); C(001D2910,0,&r);
    if (!r) { base=R(0x275670)+(uint32_t)slot*4; append_ref(o,base,0x2514B0,2); }
    base=R(0x275670)+(uint32_t)slot*4; count=R(mesh+4); append_ref(o,base,mesh+0x40,count);
    C(001D1F20,slot); return 0;
}

static uint32_t e001D4640(L15S *o, uint32_t a, uint32_t b) { return e001D4440(o,3,a,b); }

static uint32_t e001CB200(L15S *o, uint32_t self, uint32_t mesh)
{
    uint32_t saved=R(R(0x275670)+0x1C), base;
    C(001D8C20,6); C(001C7420,self,0x3F5,3); (void)e001D4640(o,mesh,self+0x80);
    base=R(0x275670); WB(R(base+0x1C)+3,0x60); W(R(base+0x1C)+4,0); WH(R(base+0x1C),0);
    W(base+0x1C,R(base+0x1C)+0x10); C(001CAAC0,R(0x275B44)+0xB0,saved); C(001D8C20,0); return 0;
}
static uint32_t e001CB2B0(L15S *o, uint32_t self) { return e001CB200(o,self,R(self+0x44)); }

static uint32_t e0020F170(L15S *o)
{
    C(0020A7A0,UINT64_C(0x2003C8A59D421DB0));
    C(00207E40,1,0x7BC0,0x7CF0,0x80,0x80,0x40808080,UINT64_C(0x2003C405DD321D00));
    C(00207E40,1,0x7800,0x8300,0x100,0x80,0x40808080,UINT64_C(0x2003CA85E1421D40));
    C(00207D00,1,3);
    C(00207E40,1,0x7000,0x8300,0x80,0x80,0x80808080,UINT64_C(0x2003CA25DD421D20));
    C(00207E40,1,0x7100,0x7900,0x80,0x40,0x80808080,UINT64_C(0x2003C8859D421DA0)); return 0;
}

int em_level15_support_001418F0(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001418F0(&o, a0, a1);
    return l15s_end(&o);
}

int em_level15_support_00153EA0(const EmLevel15SupportHooks *h, uint32_t a0, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e00153EA0(&o, a0);
    return l15s_end(&o);
}

int em_level15_support_00185420(const EmLevel15SupportHooks *h, int32_t a0, int32_t *result, EmLevel15SupportFault *fault)
{
    L15S o;
    if (!result || l15s_begin(&o, h, fault)) return -1;
    uint32_t value = e00185420(&o, a0);
    if (l15s_failed(&o)) return -1;
    *result = (int32_t)value;
    return 0;
}

int em_level15_support_001A91C0(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, float a2, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001A91C0(&o, a0, a1, l15s_bits(a2));
    return l15s_end(&o);
}

int em_level15_support_001BEB70(const EmLevel15SupportHooks *h, uint32_t a0, int32_t *result, EmLevel15SupportFault *fault)
{
    L15S o;
    if (!result || l15s_begin(&o, h, fault)) return -1;
    uint32_t value = e001BEB70(&o, a0);
    if (l15s_failed(&o)) return -1;
    *result = (int32_t)value;
    return 0;
}

int em_level15_support_001BEC40(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, int32_t a2, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001BEC40(&o, a0, a1, a2);
    return l15s_end(&o);
}

int em_level15_support_001C2690(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001C2690(&o, a0, a1, a2);
    return l15s_end(&o);
}

int em_level15_support_001CB200(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001CB200(&o, a0, a1);
    return l15s_end(&o);
}

int em_level15_support_001CB2B0(const EmLevel15SupportHooks *h, uint32_t a0, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001CB2B0(&o, a0);
    return l15s_end(&o);
}

int em_level15_support_001D4440(const EmLevel15SupportHooks *h, int32_t a0, uint32_t a1, uint32_t a2, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001D4440(&o, a0, a1, a2);
    return l15s_end(&o);
}

int em_level15_support_001D4640(const EmLevel15SupportHooks *h, uint32_t a0, uint32_t a1, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e001D4640(&o, a0, a1);
    return l15s_end(&o);
}

int em_level15_support_001D6580(const EmLevel15SupportHooks *h, int32_t a0, uint32_t a1, uint32_t a2, int32_t a3, int32_t *result, EmLevel15SupportFault *fault)
{
    L15S o;
    if (!result || l15s_begin(&o, h, fault)) return -1;
    uint32_t value = e001D6580(&o, a0, a1, a2, a3);
    if (l15s_failed(&o)) return -1;
    *result = (int32_t)value;
    return 0;
}

int em_level15_support_0020F170(const EmLevel15SupportHooks *h, EmLevel15SupportFault *fault)
{
    L15S o;
    if (l15s_begin(&o, h, fault)) return -1;
    (void)e0020F170(&o);
    return l15s_end(&o);
}
