/* Original fire sub-machines. 00170A60 is byte-matched; the other five
 * translations were checked against their original instructions because
 * their decomp C bodies are NEARMISS. See tools/test_aim_fire_machines_reference.py. */
#include "game/em_aim_fire_machines.h"
#include <stddef.h>
#include <string.h>

#define TRY(x) do { if ((x) < 0) return -1; } while (0)
#ifdef EM_AIM_FIRE_OBSERVE
extern void em_aim_fire_observe(const void *, uint32_t, unsigned);
#define OBS(p,v,n) em_aim_fire_observe((p),(v),(n))
#else
#define OBS(p,v,n) ((void)0)
#endif
static void byte(uint8_t *p, unsigned v) { *p = (uint8_t)v; OBS(p,*p,1); }
static void half(void *p, unsigned v) { uint16_t h=(uint16_t)v; memcpy(p,&h,2); OBS(p,h,2); }
static void word(void *p, uint32_t v) { memcpy(p,&v,4); OBS(p,v,4); }
static unsigned b(const EmPlayerLiveActor *a, unsigned o) { return em_live_u8(a,o); }
static int h(const EmPlayerLiveActor *a, unsigned o) { return (int16_t)em_live_u16(a,o); }
static void pb(EmPlayerLiveActor *a,unsigned o,unsigned v) { byte(a->bytes+o,v); }
static void ph(EmPlayerLiveActor *a,unsigned o,unsigned v) { half(a->bytes+o,v); }
static void pw(EmPlayerLiveActor *a,unsigned o,uint32_t v) { word(a->bytes+o,v); }

int em_aim_fire_machine_bound(const EmAimFireMachineWorkers *w, unsigned n)
{
    if (!w || !w->scene || n>5 || !w->action || !w->sound || !w->matrix ||
        !w->bone || !w->link20) return 0;
    const EmAimFireMachineScene *s=w->scene;
    switch(n) {
    case 0: return s->aim_active && s->fire_mode && s->magazine && s->reserve &&
        s->pressed && s->fire_mask && w->steer && w->reload && w->empty_sound && w->to_int;
    case 1: case 2: return s->ammo1 && w->to_int;
    case 3: return s->ammo3 && w->to_int;
    case 4: return s->ammo4 && w->reload4 && w->stop_sound;
    case 5: return s->ammo5 && s->remote && s->pressed && s->remote_mask && w->to_int;
    }
    return 0;
}
static int sound(const EmAimFireMachineWorkers *w, EmPlayerLiveActor *a, int id)
{ int unused; return w->sound(w->context,a,id,&unused); }
static int linked(const EmAimFireMachineWorkers *w, EmPlayerLiveActor *a, unsigned event)
{
    uint8_t *p=NULL;
    TRY(w->link20(w->context,em_live_u32(a,0x20),&p));
    if (!p) return -1;
    half(p,event);
    return 0;
}
static int tail(const EmAimFireMachineWorkers *w, EmPlayerLiveActor *a)
{
    int reload=b(a,0x1F0)==0x33;
    TRY(w->matrix(w->context,a));
    if (reload) return 0;
    uint32_t matrix[16];
    TRY(w->bone(w->context,4,matrix));
    if (b(a,0x1F0)==0x32 || b(a,0x1F0)==0x35 || b(a,0x275)==4 || b(a,0x2F2)) {
        /* 00102958 copies four whole quadwords in ascending order. */
        for (unsigned i=0;i<16;i++) pw(a,0x2A0+i*4,matrix[i]);
    } else {
        for (unsigned i=0;i<3;i++) pw(a,0x2D0+i*4,matrix[12+i]);
    }
    return 0;
}
static void reload_state(EmPlayerLiveActor *a)
{ pb(a,6,b(a,6)+1); pb(a,7,0); pb(a,0x1F0,0x33); }
static int release(const EmAimFireMachineWorkers *w, EmPlayerLiveActor *a, int *released)
{
    int result; TRY(w->action(w->context,a,&result));
    *released=0;
    if (!result) {
        if (!b(a,0x274)) *released=1;
        else pb(a,0x274,0);
    }
    return 0;
}
static int rifle_shot(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a)
{
    TRY(linked(w,a,1));
    byte(w->scene->magazine,*w->scene->magazine-1);
    half(w->scene->reserve,*w->scene->reserve-1);
    return sound(w,a,b(a,0x1F0)==0x31 || b(a,0x1F0)==0x34 ? 0x164 : 0x165);
}
static int convert(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a,int32_t *result)
{ return w->to_int(w->context,em_live_u32(a,0x2F4),result); }

int em_aim_fire_00170A60(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a,int arg)
{
    if (!a || !em_aim_fire_machine_bound(w,0)) return -1;
    EmAimFireMachineScene *s=w->scene;
    int r; int32_t limit; unsigned st;
    if (!arg && *s->aim_active) TRY(w->steer(w->context,a));
    st=b(a,7);
    switch(st) {
    case 0:
        ph(a,0x2E,1); TRY(w->action(w->context,a,&r));
        if (r) return tail(w,a);
        pb(a,0x2F2,1);
        if (b(a,0x274)) {
            ph(a,0x2E,0);
            if (*s->magazine) pb(a,7,!*s->fire_mode?10:*s->fire_mode==1?20:30);
            else {
                pb(a,0x274,0); TRY(sound(w,a,0x169));
                if (*s->fire_mode) pb(a,7,b(a,7)+1);
            }
        } else if (*s->pressed & 0x200) {
            TRY(w->reload(w->context,a,2,&r)); if (!r) reload_state(a);
        }
        break;
    case 1: case 23: case 32:
        TRY(release(w,a,&r)); if (r) pb(a,7,0);
        if (st==23 && (*s->pressed & 0x200)) {
            TRY(w->reload(w->context,a,2,&r));
            if (!r) { reload_state(a); pb(a,0x274,0); }
        }
        break;
    case 10:
        pb(a,7,st+1); pb(a,0x274,0); pb(a,0x2F2,0);
        TRY(rifle_shot(w,a)); ph(a,0x2A,0);
        /* fall through */
    case 11: {
        ph(a,0x276,h(a,0x276)+2);
        int count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count >= (int32_t)((uint32_t)limit-8) && (*s->pressed & *s->fire_mask)) ph(a,0x2A,1);
        count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count>=limit) {
            ph(a,0x276,0);
            if (!*s->magazine) {
                TRY(w->reload(w->context,a,1,&r));
                if (!r) reload_state(a); else pb(a,7,0);
            } else if (h(a,0x2A)) {
                pb(a,0x2F2,1); pb(a,0x274,1); pb(a,7,b(a,7)-1);
            } else pb(a,7,0);
        }
        break;
    }
    case 20:
        pb(a,7,st+1); ph(a,0x28,0);
        /* fall through */
    case 21:
        pb(a,7,b(a,7)+1); pb(a,0x274,0);
        if (h(a,0x28)<2) pw(a,0x2F4,0x41400000);
        pb(a,0x2F2,0); TRY(rifle_shot(w,a));
        /* fall through */
    case 22: {
        ph(a,0x276,h(a,0x276)+2);
        int count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count>=limit) {
            ph(a,0x276,0); pb(a,0x2F2,1); ph(a,0x28,h(a,0x28)+1);
            if (*s->magazine) {
                if (h(a,0x28)>=3) pb(a,7,b(a,7)+1);
                else { TRY(w->action(w->context,a,&r)); if (!r) pb(a,7,b(a,0x274)?b(a,7)-1:0); }
            } else {
                TRY(w->reload(w->context,a,1,&r));
                if (!r) reload_state(a);
                else {
                    if (h(a,0x28)<3) TRY(w->empty_sound(w->context));
                    pb(a,7,b(a,7)+1);
                }
            }
        }
        break;
    }
    case 30:
        pb(a,7,st+1); pb(a,0x274,0); pw(a,0x2F4,0x41400000); pb(a,0x2F2,0);
        TRY(rifle_shot(w,a));
        /* fall through */
    case 31: {
        ph(a,0x276,h(a,0x276)+2);
        int count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count>=limit) {
            ph(a,0x276,0); pb(a,0x2F2,1);
            if (*s->magazine) {
                TRY(w->action(w->context,a,&r));
                if (!r) {
                    if (b(a,0x274)) { pb(a,7,b(a,7)-1); pb(a,0x274,0); }
                    else pb(a,7,0);
                }
            } else {
                TRY(w->reload(w->context,a,1,&r));
                if (!r) reload_state(a);
                else { TRY(w->empty_sound(w->context)); pb(a,7,b(a,7)+1); }
            }
        }
        break;
    }
    default: break;
    }
    return tail(w,a);
}

/* 00171320, 00171670 and 00171B00 share firing operations but differ at
 * the acceptance, dry-fire and recovery boundaries. */
static int auxiliary(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a,unsigned kind)
{
    if (!a || !em_aim_fire_machine_bound(w,kind)) return -1;
    uint16_t *ammo=kind==3?w->scene->ammo3:w->scene->ammo1;
    unsigned st=b(a,7); int r; int32_t limit;
    if (st==0 && kind!=1) { pb(a,7,st+1); ph(a,0x28,0); st=1; }
    if (st==(kind==1?0u:1u)) {
        ph(a,0x2E,1); TRY(w->action(w->context,a,&r));
        if (r) return tail(w,a);
        pb(a,0x2F2,1);
        if (b(a,0x274)) {
            ph(a,0x2E,0);
            if (!*ammo) {
                if (kind==2) pb(a,7,b(a,7)+1);
                pb(a,0x274,0); TRY(sound(w,a,0x17A));
            } else pb(a,7,10);
        }
    } else if (kind==2 && (st==2 || st==12)) {
        TRY(release(w,a,&r));
        if (r) pb(a,7,st==2?0:1);
    } else if (st==10 || st==11) {
        if (st==10) {
            pb(a,7,11); pb(a,0x274,0); pb(a,0x2F2,0);
            TRY(linked(w,a,1)); half(ammo,*ammo-1);
            TRY(sound(w,a,b(a,0x1F0)==0x31 || b(a,0x1F0)==0x34?0x5DC:0x5DD));
        }
        ph(a,0x276,h(a,0x276)+1);
        int count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count>=limit) {
            ph(a,0x276,0);
            if (kind==1) {
                if (*ammo) { pb(a,0x1F0,0x33); pb(a,6,b(a,6)+1); }
                pb(a,7,0);
            } else {
                ph(a,0x28,h(a,0x28)+1);
                if (kind==2) {
                    pb(a,0x2F2,1);
                    if (*ammo) {
                        if (h(a,0x28)>=6) { pb(a,0x1F0,0x33); pb(a,6,b(a,6)+1); pb(a,7,0); }
                        else { TRY(w->action(w->context,a,&r)); if (!r) pb(a,7,b(a,0x274)?b(a,7)-1:1); }
                    } else if (h(a,0x28)<6) { TRY(sound(w,a,0x17A)); pb(a,7,b(a,7)+1); }
                    else pb(a,7,1);
                } else {
                    if (!*ammo || h(a,0x28)<6) pb(a,7,1);
                    else { pb(a,0x1F0,0x33); pb(a,6,b(a,6)+1); pb(a,7,0); }
                }
            }
        }
    }
    return tail(w,a);
}
int em_aim_fire_00171320(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a) { return auxiliary(w,a,1); }
int em_aim_fire_00171670(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a) { return auxiliary(w,a,2); }
int em_aim_fire_00171B00(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a) { return auxiliary(w,a,3); }

static int finish4(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a)
{
    int r; TRY(w->reload4(w->context,&r));
    if (!r) reload_state(a); else pb(a,7,0);
    return linked(w,a,2);
}
int em_aim_fire_00171E90(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a)
{
    if (!a || !em_aim_fire_machine_bound(w,4)) return -1;
    uint16_t *ammo=w->scene->ammo4;
    unsigned st=b(a,7); int r;
    switch(st) {
    case 0:
        pb(a,7,st+1); if (!*ammo) TRY(w->reload4(w->context,&r));
        /* fall through */
    case 1:
        ph(a,0x2E,1); TRY(w->action(w->context,a,&r));
        if (r) return tail(w,a);
        pb(a,0x2F2,1);
        if (b(a,0x274)) {
            ph(a,0x2E,0);
            if (!*ammo) { pb(a,7,b(a,7)+1); pb(a,0x274,0); TRY(sound(w,a,0x17A)); }
            else pb(a,7,10);
        }
        break;
    case 2:
        TRY(release(w,a,&r)); if (r) pb(a,7,0);
        break;
    case 10:
        pb(a,7,st+1); pb(a,0x274,0); ph(a,0x28,4);
        TRY(linked(w,a,1)); half(ammo,*ammo-1); pb(a,0x2F2,0);
        TRY(sound(w,a,0x5DC)); pb(a,0x2F1,0xFF);
        break;
    case 11: {
        int count=h(a,0x28); ph(a,0x28,count-1);
        if (!count) {
            if (!*ammo) TRY(finish4(w,a));
            else {
                pb(a,7,b(a,7)+1); TRY(w->sound(w->context,a,0x5DD,&r));
                pb(a,0x31B,r); pb(a,0x31A,1); ph(a,0x31C,0x5DD); ph(a,0x28,0);
            }
        }
        break;
    }
    case 12:
        TRY(w->action(w->context,a,&r));
        if (!r) {
            if (b(a,0x274)) {
                pb(a,0x274,0);
                if (!(h(a,0x28)%3)) TRY(linked(w,a,1));
                ph(a,0x28,h(a,0x28)+1);
                if (h(a,0x28)>=6) { ph(a,0x28,0); half(ammo,*ammo-1); if (!*ammo) TRY(finish4(w,a)); }
            } else pb(a,7,0);
        }
        if (b(a,7)!=12) {
            int handle=(int8_t)b(a,0x31B);
            if (handle!=-1) { TRY(w->stop_sound(w->context,handle)); pb(a,0x31B,0xFF); pb(a,0x31A,0); }
        } else if (b(a,0x31A) && em_live_u16(a,0x31C)==0x5DD && (int8_t)b(a,0x31B)==-1) {
            TRY(w->sound(w->context,a,0x5DD,&r)); pb(a,0x31B,r);
        }
        break;
    default: break;
    }
    return tail(w,a);
}
static void remote_press(const EmAimFireMachineWorkers *w)
{
    EmAimFireMachineScene *s=w->scene;
    if (*s->remote==1 && (*s->pressed & *s->remote_mask)) byte(s->remote,2);
}
static void finish5(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a)
{
    if (*w->scene->ammo5) { pb(a,0x1F0,0x33); pb(a,6,b(a,6)+1); }
    pb(a,7,0);
}
int em_aim_fire_001723D0(const EmAimFireMachineWorkers *w,EmPlayerLiveActor *a)
{
    if (!a || !em_aim_fire_machine_bound(w,5)) return -1;
    EmAimFireMachineScene *s=w->scene;
    unsigned st=b(a,7); int r; int32_t limit;
    switch(st) {
    case 0:
        if (*s->remote==1) { remote_press(w); break; }
        if (*s->remote) break;
        ph(a,0x2E,1); TRY(w->action(w->context,a,&r)); if (r) return tail(w,a);
        pb(a,0x2F2,1);
        if (b(a,0x274)) {
            ph(a,0x2E,0);
            if (!*s->ammo5) { pb(a,0x274,0); TRY(sound(w,a,0x17A)); }
            else pb(a,7,10);
        }
        break;
    case 10:
        pb(a,7,st+1); pb(a,0x274,0); pb(a,0x2F2,0);
        /* fall through */
    case 11:
        ph(a,0x276,h(a,0x276)+1);
        if (h(a,0x276)>=11) {
            pb(a,7,b(a,7)+1); TRY(linked(w,a,1)); half(s->ammo5,*s->ammo5-1);
            TRY(sound(w,a,b(a,0x1F0)==0x31 || b(a,0x1F0)==0x34?0x5DC:0x5DD));
            byte(s->remote,1);
        }
        break;
    case 12: {
        remote_press(w); ph(a,0x276,h(a,0x276)+1);
        int count=h(a,0x276); TRY(convert(w,a,&limit));
        if (count>=limit) {
            ph(a,0x276,0); pb(a,0x2F2,1);
            if (!*s->remote) finish5(w,a); else pb(a,7,b(a,7)+1);
        }
        break;
    }
    case 13:
        if (!*s->remote) finish5(w,a); else remote_press(w);
        break;
    default: break;
    }
    return tail(w,a);
}
