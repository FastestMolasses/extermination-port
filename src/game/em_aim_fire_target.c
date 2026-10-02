#include "game/em_aim_fire_target.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { EmAimFireTarget *h; uint32_t entry, sp; } Run;
#define A 0x700038A0u
#define B 0x700038B0u
#define C 0x700038C0u
#define D 0x700038D0u
#define HIT 0x700031B0u
#define HIT_NODE 0x700031D4u
#define LOCK 0x008106E0u
#define MODE 0x00810CA4u
#define ONE 0x3F800000u
#define RANGE 0x43820000u
#define CHECK() do { if (r->h->fault) return -1; } while (0)
#define ENTER(fn, frame) Run run = {h, fn, h ? h->sp - frame : 0}, *r = &run; \
    if (!h || h->fault) return -1

static void fault(Run *r, int code, uint32_t address)
{
    if (!r->h->fault) {
        r->h->fault = code; r->h->fault_function = r->entry;
        r->h->fault_address = address;
    }
}
static uint8_t *memory(Run *r, uint32_t address, uint32_t size, int write)
{
    uint8_t *p;
    if (r->h->fault) return NULL;
    if (!r->h->map) { fault(r, 1, address); return NULL; }
    p = r->h->map(r->h->context, address, size, write);
    if (!p) fault(r, 2, address);
    return p;
}
static uint32_t readn(Run *r, uint32_t a, unsigned n)
{
    uint8_t *p = memory(r, a, n, 0); uint32_t v = 0;
    if (p) for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint32_t word(Run *r, uint32_t a) { return readn(r, a, 4); }
static void put(Run *r, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = memory(r, a, n, 1);
    if (!p) return;
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8 * i));
    if (r->h->store) r->h->store(r->h->context, a, n);
}
static void store(Run *r, uint32_t a, uint32_t v) { put(r, a, v, 4); }
static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
static EmAimFireTargetCall call(Run *r, uint32_t fn, unsigned na, unsigned nf,
                               uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
                               uint64_t a4, uint32_t f0, uint32_t f1, uint32_t f2)
{
    EmAimFireTargetCall c = {fn, r->sp, {a0,a1,a2,a3,a4}, {f0,f1,f2}, na,nf,0,0};
    if (r->h->fault) return c;
    if (!r->h->call) fault(r, 1, fn);
    else if (r->h->call(r->h->context, &c) < 0) fault(r, 3, fn);
    return c;
}
static uint32_t target_call0(Run *r, uint32_t fn) { return (uint32_t)call(r,fn,0,0,0,0,0,0,0,0,0,0).v0; }
static uint32_t c1(Run *r, uint32_t fn, uint32_t a)
{ return (uint32_t)call(r,fn,1,0,a,0,0,0,0,0,0,0).v0; }
static void c2(Run *r, uint32_t fn, uint32_t a, uint32_t b)
{ (void)call(r,fn,2,0,a,b,0,0,0,0,0,0); }
static void c3(Run *r, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{ (void)call(r,fn,3,0,a,b,c,0,0,0,0,0); }
static void scale(Run *r, uint32_t a, uint32_t b, uint32_t f)
{ (void)call(r,0x103230,2,1,a,b,0,0,0,f,0,0); }
static uint32_t unary(Run *r, uint32_t fn, uint32_t f)
{ return call(r,fn,0,1,0,0,0,0,0,f,0,0).f0; }
static uint32_t segment(Run *r, uint32_t a, uint32_t b, uint32_t mask, uint32_t id)
{ return (uint32_t)call(r,0x19A570,4,0,a,b,mask,id,0,0,0,0).v0; }
static uint32_t distance(Run *r, uint32_t from, uint32_t to)
{
    uint32_t x = em_ee_sub_bits(word(r,from),word(r,to));
    uint32_t y = em_ee_sub_bits(word(r,from+4),word(r,to+4));
    uint32_t z = em_ee_sub_bits(word(r,from+8),word(r,to+8));
    uint32_t acc = em_ee_adda_bits(em_ee_mul_bits(x,x),em_ee_mul_bits(y,y));
    return unary(r,0x11E748,em_ee_madd_bits(acc,z,z));
}
static uint32_t angle(Run *r, uint32_t actor, uint32_t target)
{
    uint32_t x = word(r,target+0xB0), z = word(r,target+0xB8);
    uint32_t f = call(r,0x1B1240,1,2,actor+0xA0,0,0,0,0,x,z,0).f0;
    f = unary(r,0x1B1470,em_ee_sub_bits(f,word(r,actor+0xC4)));
    return unary(r,0x11DF78,f);
}
static void delta(Run *r, uint32_t dest, uint32_t a, uint32_t b)
{
    uint32_t x = em_ee_sub_bits(word(r,a),word(r,b));
    uint32_t y = em_ee_sub_bits(word(r,a+4),word(r,b+4));
    uint32_t z = em_ee_sub_bits(word(r,a+8),word(r,b+8));
    store(r,dest,x); store(r,dest+4,y); store(r,dest+8,z); store(r,dest+12,ONE);
}
static uint32_t dot(Run *r, uint32_t gun)
{
    c2(r,0x102760,A,C);
    uint32_t f = call(r,0x102738,2,0,gun+0xC0,A,0,0,0,0,0,0).f0;
    store(r,0x70003A20,f); return f;
}
static int visible(Run *r, uint32_t from, uint32_t dest, uint32_t target)
{
    if (!segment(r,from,dest,1,0x20)) return 0;
    if (word(r,HIT_NODE) != target) return 0;
    return segment(r,from,HIT,6,0) == 0;
}

int em_aim_fire_target_00185E30(EmAimFireTarget *h, uint32_t actor, uint32_t target, uint32_t *result)
{
    ENTER(0x185E30,0x50);
    uint32_t gun = word(r,actor+0x20), answer = 0;
    c2(r,0x102948,B,gun+0xA0); CHECK();
    if ((readn(r,target,1)&1) && c1(r,0x183AC0,target) && readn(r,target+0x34,2)) {
        uint32_t yaw = angle(r,actor,target); CHECK();
        if (em_ee_c_le_bits(yaw,0x3FC90FDB)) {
            c2(r,0x183C40,target,D);
            uint32_t d = distance(r,actor+0xA0,D); CHECK();
            if (em_ee_c_lt_bits(d,RANGE)) {
                delta(r,C,D,B);
                uint32_t v = dot(r,gun); CHECK();
                if (!em_ee_c_le_bits(v,0x3F000000)) {
                    scale(r,C,C,0x3F99999A); c3(r,0x1028B8,D,B,C); CHECK();
                    if (visible(r,B,D,target)) answer = target;
                }
            }
        }
    }
    CHECK(); if (result) *result = answer; return 0;
}

int em_aim_fire_target_00185A10(EmAimFireTarget *h, uint32_t actor, uint32_t *result)
{
    ENTER(0x185A10,0x80);
    uint32_t gun = word(r,actor+0x20), answer = 0;
    c2(r,0x102948,B,gun+0xA0);
    uint32_t flags = target_call0(r,0x1B0070); CHECK();
    uint32_t radius = (flags&0x80) && !readn(r,0x8106C7,1) ? 0x425C0000 : 0x42DC0000;
    scale(r,D,gun+0xC0,radius); c3(r,0x1028B8,D,D,gun+0xA0); store(r,D+12,ONE); CHECK();
    if (segment(r,B,D,1,0x20)) {
        uint32_t target = word(r,HIT_NODE); CHECK();
        if (target && (readn(r,target,1)&1) && c1(r,0x183AC0,target) &&
            readn(r,target+0x34,2) && !segment(r,B,HIT,6,0)) {
            answer = word(r,target+0x14); CHECK();
            if (result) *result = answer; return 0;
        }
    }
    int32_t count = (int16_t)readn(r,0x275B94,2);
    uint32_t list = word(r,0x275B8C); CHECK();
    while (count) {
        uint32_t target = word(r,list); list += 4; --count; CHECK();
        if (!(readn(r,target,1)&1) || !c1(r,0x183AC0,target) || !readn(r,target+0x34,2)) { CHECK(); continue; }
        uint32_t yaw = angle(r,actor,target); CHECK();
        if (!em_ee_c_le_bits(yaw,0x3FC90FDB)) continue;
        c2(r,0x183C40,target,D);
        uint32_t d = distance(r,actor+0xA0,D); CHECK();
        if (!em_ee_c_lt_bits(d,radius)) continue;
        delta(r,C,D,B);
        uint32_t v = dot(r,gun); CHECK();
        uint32_t threshold = em_ee_c_le_bits(d,0x420C0000) ? 0x3F350481 : 0x3F51B717;
        if (em_ee_c_lt_bits(v,threshold) || !em_ee_c_le_bits(yaw,0x3F860A92)) continue;
        scale(r,C,C,0x3F99999A); c3(r,0x1028B8,D,B,C); CHECK();
        if (visible(r,B,D,target)) { answer = word(r,target+0x14); radius = d; }
        CHECK();
    }
    if (result) *result = answer; return 0;
}

int em_aim_fire_target_00199220(EmAimFireTarget *h, uint32_t actor)
{
    ENTER(0x199220,0x80);
    uint32_t gun = word(r,actor+0x20);
    c2(r,0x102948,A,gun+0xA0);
    int32_t count = (int16_t)readn(r,0x275B94,2);
    store(r,LOCK,0); store(r,LOCK+4,0);
    uint32_t list = word(r,0x275B8C); store(r,LOCK+8,0);
    uint32_t best[3] = {0x447A0000,0x447A0000,0x447A0000}; CHECK();
    while (count) {
        uint32_t target = word(r,list); list += 4; --count; CHECK();
        if (!readn(r,target,1) || !c1(r,0x183B80,target) || !readn(r,target+0x34,2)) { CHECK(); continue; }
        c2(r,0x183C40,target,B);
        uint32_t d = distance(r,actor+0xA0,B); CHECK();
        if (!em_ee_c_lt_bits(d,RANGE)) continue;
        c2(r,0x102948,D,B); store(r,D+12,ONE); c3(r,0x1026A0,D,0x70003AC0,D); CHECK();
        uint32_t t = em_ee_div_bits(0x41800000,word(r,D+12));
        if (em_ee_c_lt_bits(t,0)) continue;
        uint32_t mode = readn(r,MODE,1);
        uint32_t x = em_ee_sub_bits(em_ee_div_bits(em_ee_mul_bits(word(r,D),t),0x41800000),0x45000000);
        uint32_t y = em_ee_sub_bits(em_ee_div_bits(em_ee_mul_bits(word(r,D+4),t),0x41800000),0x45000000);
        store(r,D,x); store(r,D+4,y); y = em_ee_mul_bits(0x3FC00000,y); store(r,D+4,y);
        int accept = 0;
        if (mode == 1) {
            uint32_t screen = unary(r,0x11E748,em_ee_madd_bits(em_ee_mula_bits(x,x),y,y));
            uint32_t threshold = em_ee_add_bits(0x42480000,em_ee_mul_bits(0x425C0000,word(r,gun+0x214)));
            accept = em_ee_c_le_bits(screen,threshold);
        } else {
            uint32_t ax = unary(r,0x11DF78,x);
            uint32_t tx = em_ee_add_bits(0x42840000,em_ee_mul_bits(0x42480000,word(r,gun+0x214)));
            if (em_ee_c_le_bits(ax,tx)) {
                uint32_t ay = unary(r,0x11DF78,word(r,D+4));
                uint32_t ty = em_ee_add_bits(0x42340000,em_ee_mul_bits(0x42340000,word(r,gun+0x214)));
                accept = em_ee_c_le_bits(ay,ty);
            }
        }
        CHECK(); if (!accept) continue;
        delta(r,D,B,A); scale(r,D,D,0x3F99999A); c3(r,0x1028B8,D,A,D); CHECK();
        if (visible(r,A,D,target)) {
            if (em_ee_c_lt_bits(d,best[0])) {
                best[2]=best[1]; best[1]=best[0]; best[0]=d;
                store(r,LOCK+8,word(r,LOCK+4)); store(r,LOCK+4,word(r,LOCK)); store(r,LOCK,target);
            } else if (em_ee_c_lt_bits(d,best[1])) {
                best[2]=best[1]; best[1]=d;
                store(r,LOCK+8,word(r,LOCK+4)); store(r,LOCK+4,target);
            } else if (em_ee_c_lt_bits(d,best[2])) { store(r,LOCK+8,target); best[2]=d; }
        }
        CHECK();
    }
    unsigned limit = readn(r,MODE,1) == 1 ? 1 : 3; CHECK();
    for (unsigned i=0;i<limit;++i) {
        uint32_t target = word(r,LOCK+4*i); CHECK();
        if (target) {
            c2(r,0x183C40,target,B);
            (void)call(r,0x1DD170,5,0,1,B,0,sx(0x80808080),0,0,0,0); CHECK();
        }
    }
    return 0;
}

static int sight(Run *r, uint32_t gun)
{
    scale(r,A,gun+0xC0,RANGE); c3(r,0x1028B8,A,A,gun+0xA0);
    store(r,A+12,ONE); c2(r,0x102948,B,gun+0xA0); CHECK();
    uint32_t hit = segment(r,B,A,7,0x20); CHECK();
    if (hit) {
        uint32_t target = word(r,HIT_NODE); CHECK();
        if (target && c1(r,0x183AC0,target)) put(r,target+0xA,0x80,1);
        c2(r,0x1031E0,A,HIT); store(r,gun+0xBC,ONE); store(r,gun+0x210,1);
        c2(r,0x102948,gun+0x200,A); CHECK();
    }
    return hit != 0;
}
static void colour(Run *r,uint32_t red,uint32_t green,uint32_t blue)
{ store(r,B,red); store(r,B+4,green); store(r,B+8,blue); store(r,B+12,0x80); }
static void sprite(Run *r,uint32_t size)
{
    uint32_t rgba = word(r,B) | word(r,B+4)<<8 | word(r,B+8)<<16 | word(r,B+12)<<24;
    (void)call(r,0x1CD520,5,3,0,2,A,UINT64_C(0x20045BA5154222DC),sx(rgba),size,size,0x40000000);
}
int em_aim_fire_target_001854E0(EmAimFireTarget *h, uint32_t gun)
{
    ENTER(0x1854E0,0x40);
    int hit = sight(r,gun); CHECK();
    if (hit) {
        c3(r,0x1028D0,D,A,B);
        uint32_t x=word(r,D),y=word(r,D+4),z=word(r,D+8);
        uint32_t d=unary(r,0x11E748,em_ee_madd_bits(em_ee_adda_bits(em_ee_mul_bits(x,x),em_ee_mul_bits(y,y)),z,z));
        store(r,0x70003A24,d); CHECK();
        if (em_ee_c_lt_bits(d,RANGE)) {
            d=em_ee_sub_bits(word(r,0x70003A24),0x41A00000); store(r,0x70003A24,d);
            store(r,gun+0x214,em_ee_c_lt_bits(d,0) ? ONE : em_ee_div_bits(em_ee_sub_bits(0x43700000,d),0x43700000));
        } else store(r,gun+0x214,0);
    } else store(r,gun+0x214,0);
    uint32_t random = target_call0(r,0x122BB8); CHECK();
    colour(r,((random>>15)&31)+0x40,0,0); sprite(r,0x40400000); CHECK(); return 0;
}
int em_aim_fire_target_00185760(EmAimFireTarget *h, uint32_t gun)
{
    ENTER(0x185760,0x50);
    (void)target_call0(r,0x122BB8); (void)target_call0(r,0x122BB8); CHECK();
    uint32_t local=r->sp+0x40;
    uint8_t saved[16], *p=memory(r,0x2487D0,16,0); CHECK(); memcpy(saved,p,16);
    p=memory(r,local,16,1); CHECK(); memcpy(p,saved,16);
    if (h->store) h->store(h->context,local,16);
    (void)sight(r,gun); CHECK();
    store(r,0x275B00,(word(r,0x275B00)+3)&31);
    uint32_t random=(target_call0(r,0x122BB8)>>15)&31; CHECK();
    if (word(r,LOCK)) { colour(r,random+0x70,random+0x40,random+0x20); sprite(r,0x40A00000); }
    else { colour(r,random+0x50,0,0); sprite(r,0x40400000); }
    c2(r,0x102948,C,gun+0x1F0); CHECK();
    if (word(r,LOCK)) { store(r,local,ONE); store(r,local+4,0x3F19999A); store(r,local+8,0x3E4CCCCD); store(r,local+12,ONE); }
    else { store(r,local,0x3F333333); store(r,local+4,0); store(r,local+8,0); store(r,local+12,ONE); }
    (void)call(r,0x1E2BA0,3,1,C,A,local,0,0,RANGE,0,0); CHECK(); return 0;
}


int em_aim_fire_target_00183AC0(EmAimFireTarget *h, uint32_t actor, uint32_t *result)
{
    ENTER(0x183AC0,0);
    uint32_t answer=0;
    if ((readn(r,actor+2,1)&0x1F)==2) {
        uint32_t model=readn(r,actor+3,1);
        if (model < 0x0D || model > 0x13)
            answer = model != 6 || readn(r,actor+0x9F,1)==0;
    }
    CHECK(); if (result) *result=answer; return 0;
}
int em_aim_fire_target_00183B80(EmAimFireTarget *h, uint32_t actor, uint32_t *result)
{
    ENTER(0x183B80,0);
    uint32_t answer=0;
    if ((readn(r,actor+2,1)&0x1F)==2) {
        uint32_t model=readn(r,actor+3,1);
        if (model==0x12) answer=readn(r,actor+0x0D,1)==1;
        else if (model!=0x13 && model!=0x0D && model!=0x0E && model!=0x0F)
            answer=model!=6 || readn(r,actor+0x9F,1)==0;
    }
    CHECK(); if (result) *result=answer; return 0;
}
