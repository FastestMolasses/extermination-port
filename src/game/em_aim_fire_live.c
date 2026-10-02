#include "game/em_aim_fire_live.h"
#include "game/em_aim_fire_control.h"
#include "game/em_aim_fire_machines.h"
#include "game/em_aim_fire_marker.h"
#include "game/em_aim_fire_pose.h"
#include "game/em_aim_fire_shots.h"
#include "game/em_ee_float.h"
#include <string.h>

#define LOCAL_BASE UINT32_C(0x7F001000)
#define TRY(x) do { if ((x) < 0) return -1; } while (0)
static void *region(const EmPoseRegion *r, uint32_t a, size_t n, int write)
{
    if (!r || !r->bytes || n > r->size || a < r->address ||
        (uint64_t)a + n > (uint64_t)r->address + r->size || (write && !r->writable)) return NULL;
    return r->bytes + (a - r->address);
}
static int overlaps(const EmPoseRegion *r,uint32_t a,size_t n)
{
    return r && r->size && (uint64_t)a+n>r->address &&
           (uint64_t)a<(uint64_t)r->address+r->size;
}
void *em_aim_fire_live_map(void *context, uint32_t a, size_t n, int write)
{
    EmAimFireLive *h = context;
    if (!h || h->fault_function || h->fault_address) return NULL;
    if (!n || n>UINT32_MAX || (uint64_t)a+n>UINT64_C(0x100000000)) {
        h->fault_address=a ? a : UINT32_MAX;return NULL;
    }
    EmPoseRegion owned[2]={{LOCAL_BASE,sizeof h->temporary,h->temporary,1},
        {h->player_address,h->player ? sizeof h->player->bytes : 0,h->player ? h->player->bytes : NULL,1}};
    for (unsigned i=0;i<2;++i) {
        void *p=region(&owned[i],a,n,write);
        if (p) return p;
        if (overlaps(&owned[i],a,n)) { h->fault_address=a ? a : UINT32_MAX;return NULL; }
    }
    for (unsigned i=0; h->regions && i<h->region_count; ++i) {
        void *p=region(&h->regions[i],a,n,write);
        if (p) return p;
        /* An explicit owner is authoritative. A readonly or cross-boundary
         * request cannot fall through to a second, permissive memory image. */
        if (overlaps(&h->regions[i],a,n)) { h->fault_address=a ? a : UINT32_MAX;return NULL; }
    }
    if (h->pose) for (unsigned i=0; i<h->pose->region_count && i<EM_POSE_REGION_MAX; ++i) {
        void *p=region(&h->pose->region[i],a,n,write);
        if (p) return p;
        if (overlaps(&h->pose->region[i],a,n)) { h->fault_address=a ? a : UINT32_MAX;return NULL; }
    }
    void *p=h->map ? h->map(h->context,a,n,write) : NULL;
    if (!p) h->fault_address=a ? a : UINT32_MAX;
    return h->fault_function || h->fault_address ? NULL : p;
}
static int read_word(EmAimFireLive *h,uint32_t a,uint32_t *v)
{
    const uint8_t *p=em_aim_fire_live_map(h,a,4,0);
    if (!p) return -1;
    *v=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    return 0;
}
static void signature(EmAimFireTargetCall *f);
static int invoke(EmAimFireLive *h,uint32_t fn,uint32_t a,uint32_t b,uint32_t c,
                  uint32_t f,uint32_t g,EmAimFireTargetCall *out)
{
    EmAimFireTargetCall frame={0};
    frame.function=fn; frame.a[0]=a; frame.a[1]=b; frame.a[2]=c;
    frame.f[0]=f; frame.f[1]=g; frame.na=3; frame.nf=2;
    signature(&frame);
    TRY(em_aim_fire_live_call(h,&frame));
    if (out) *out=frame;
    return 0;
}
static void signature(EmAimFireTargetCall *f)
{
    switch (f->function) {
    case 0x1C61D0: case 0x17A8B0: case 0x102958: case 0x183C40: case 0x17A800:
    case 0x102760: case 0x17B300: case 0x102738: case 0x102948: case 0x1031E0:
    case 0x1860A0: case 0x19B6C0: case 0x1CD390: f->na=2; f->nf=0; break;
    case 0x1FBD50: case 0x1749A0: f->na=3; f->nf=1; break;
    case 0x1B5DC0: case 0x17A130: case 0x16F5D0: case 0x1C6DA0: case 0x1029C0:
    case 0x11E860: case 0x1607D0: case 0x17AF70: case 0x11A070:
    case 0x1839A0: case 0x1AFA90: case 0x1F4F40: case 0x18ABA0: case 0x1B17A0: f->na=1; f->nf=0; break;
    case 0x11E2A8: case 0x1B1470: case 0x11DE90: case 0x11E748: case 0x1B1510:
    case 0x1281C0: f->na=0; f->nf=1; break;
    case 0x11E620: f->na=0; f->nf=2; break;
    case 0x102B08: case 0x102BB0: case 0x102900: case 0x103230: f->na=2; f->nf=1; break;
    case 0x102918: case 0x1026A0: case 0x102718: case 0x1028B8: case 0x1028D0:
    case 0x1EFD90: f->na=3; f->nf=0; break;
    case 0x1F00A0: f->na=4; f->nf=0; break;
    case 0x1EFD20: f->na=2; f->nf=0; break;
    case 0x1B1240: f->na=1; f->nf=2; break;
    case 0x1E8B90: f->na=f->nf=1; break;
    case 0x19A570: f->na=4; f->nf=0; break;
    case 0x1B41F0: f->na=6; f->nf=0; break;
    case 0x17B420: case 0x15D2F0: case 0x122BB8: f->na=f->nf=0; break;
    default: break;
    }
}
static int control_call(void *context,uint32_t fn,EmAimFireControlCall *f)
{
    EmAimFireLive *h=context;
    EmAimFireTargetCall frame={0};
    frame.function=fn; frame.na=4; frame.nf=3;
    for (unsigned i=0;i<4;++i) frame.a[i]=f->a[i];
    for (unsigned i=0;i<3;++i) frame.f[i]=f->f[i];
    signature(&frame);
    TRY(em_aim_fire_live_call(h,&frame));
    f->v0=(uint32_t)frame.v0; f->f0=frame.f0;
    return 0;
}
static int pose_call(void *context,uint32_t fn,const uint32_t a[3],const uint32_t f[2],uint32_t *out)
{
    EmAimFireTargetCall frame;
    TRY(invoke(context,fn,a[0],a[1],a[2],f[0],f[1],&frame));
    if (out) *out=(uint32_t)frame.v0;
    return 0;
}
static int shots_call(void *context,uint32_t fn,EmAimFireShotsCall *f)
{
    EmAimFireTargetCall frame={0};
    frame.function=fn;frame.na=6;frame.nf=4;
    for (unsigned i=0;i<6;++i) frame.a[i]=f->a[i];
    for (unsigned i=0;i<4;++i) frame.f[i]=f->f[i];
    signature(&frame);
    TRY(em_aim_fire_live_call(context,&frame));
    f->v0=(uint32_t)frame.v0;f->f0=frame.f0;return 0;
}
static uint8_t *target_map(void *context,uint32_t a,uint32_t n,int write)
{ return em_aim_fire_live_map(context,a,n,write); }
static int target_call(void *context,EmAimFireTargetCall *frame)
{ return em_aim_fire_live_call(context,frame); }
static int player_address(EmAimFireLive *h,const EmPlayerLiveActor *p,uint32_t *a)
{
    if (!p || p!=h->player) return -1;
    *a=h->player_address;
    return 0;
}
static int machine_action(void *context,EmPlayerLiveActor *p,int *result)
{
    EmAimFireLive *h=context; uint32_t a; EmAimFireTargetCall frame;
    TRY(player_address(h,p,&a)); TRY(invoke(h,0x1607D0,a,0,0,0,0,&frame));
    *result=em_ee_word_int((uint32_t)frame.v0); return 0;
}
static int machine_steer(void *context,EmPlayerLiveActor *p)
{
    EmAimFireLive *h=context; uint32_t a;
    TRY(player_address(h,p,&a)); return invoke(h,0x17AF70,a,0,0,0,0,NULL);
}
static int machine_reload(void *context,EmPlayerLiveActor *p,int arg,int *result)
{
    EmAimFireLive *h=context; uint32_t a; EmAimFireTargetCall frame;
    TRY(player_address(h,p,&a)); TRY(invoke(h,0x17B300,a,(uint32_t)arg,0,0,0,&frame));
    *result=em_ee_word_int((uint32_t)frame.v0); return 0;
}
static int machine_reload4(void *context,int *result)
{
    EmAimFireTargetCall frame; TRY(invoke(context,0x17B420,0,0,0,0,0,&frame));
    *result=em_ee_word_int((uint32_t)frame.v0); return 0;
}
static int machine_sound(void *context,EmPlayerLiveActor *p,int id,int *handle)
{
    EmAimFireLive *h=context; uint32_t a; EmAimFireTargetCall frame;
    TRY(player_address(h,p,&a)); TRY(invoke(h,0x1FBD50,a,(uint32_t)id,0,0x43960000,0,&frame));
    *handle=em_ee_word_int((uint32_t)frame.v0); return 0;
}
static int machine_empty_sound(void *context)
{
    EmAimFireTargetCall frame={0};
    frame.function=0x1FB9F0; frame.na=4;
    frame.a[0]=0x169; frame.a[1]=frame.a[2]=frame.a[3]=0x1000;
    return em_aim_fire_live_call(context,&frame);
}
static int machine_stop_sound(void *context,int handle)
{ return invoke(context,0x11A070,(uint32_t)handle,0,0,0,0,NULL); }
static int machine_matrix(void *context,EmPlayerLiveActor *p)
{
    EmAimFireLive *h=context; uint32_t a;
    TRY(player_address(h,p,&a)); return invoke(h,0x17A130,a,0,0,0,0,NULL);
}
static int machine_bone(void *context,unsigned slot,uint32_t words[16])
{
    EmAimFireLive *h=context; uint32_t table,node;
    TRY(read_word(h,0x275B40,&table)); TRY(read_word(h,table+4*slot,&node));
    const void *p=em_aim_fire_live_map(h,node+0x90,64,0);
    if (!p) return -1;
    memcpy(words,p,64); return 0;
}
static int machine_link(void *context,uint32_t a,uint8_t **p)
{
    *p=em_aim_fire_live_map(context,a+0x2E,2,1); return *p ? 0 : -1;
}
static int machine_int(void *context,uint32_t bits,int32_t *result)
{
    EmAimFireTargetCall frame; TRY(invoke(context,0x1281C0,0,0,0,bits,0,&frame));
    *result=em_ee_word_int((uint32_t)frame.v0); return 0;
}
static int machine(EmAimFireLive *h,uint32_t fn,uint32_t a,int32_t arg)
{
    if (!h->player || a!=h->player_address) return -1;
    EmAimFireMachineScene scene={0};
#define MAP(a,n,w) em_aim_fire_live_map(h,a,n,w)
    switch(fn) {
    case 0x170A60:
        scene.aim_active=MAP(0x8106E0,4,0); scene.fire_mode=MAP(0x810C61,1,0);
        scene.magazine=MAP(0x810C62,1,1); scene.reserve=MAP(0x810CB4,2,1);
        scene.pressed=MAP(0x810E74,2,0); scene.fire_mask=MAP(0x70003B78,2,0); break;
    case 0x171320: case 0x171670: scene.ammo1=MAP(0x810CAA,2,1); break;
    case 0x171B00: scene.ammo3=MAP(0x810CA8,2,1); break;
    case 0x171E90: scene.ammo4=MAP(0x810CAE,2,1); break;
    case 0x1723D0:
        scene.ammo5=MAP(0x810CB0,2,1); scene.remote=MAP(0x810CB6,1,1);
        scene.pressed=MAP(0x810E74,2,0); scene.remote_mask=MAP(0x70003B74,2,0); break;
    default:return -1;
    }
#undef MAP
    if (h->fault_address) return -1;
    EmAimFireMachineWorkers w={h,&scene,machine_action,machine_steer,machine_reload,machine_reload4,
        machine_sound,machine_empty_sound,machine_stop_sound,machine_matrix,machine_bone,machine_link,machine_int};
    switch(fn) {
    case 0x170A60:return em_aim_fire_00170A60(&w,h->player,arg);
    case 0x171320:return em_aim_fire_00171320(&w,h->player);
    case 0x171670:return em_aim_fire_00171670(&w,h->player);
    case 0x171B00:return em_aim_fire_00171B00(&w,h->player);
    case 0x171E90:return em_aim_fire_00171E90(&w,h->player);
    case 0x1723D0:return em_aim_fire_001723D0(&w,h->player);
    default:return -1;
    }
}
static int dispatch(EmAimFireLive *h,EmAimFireTargetCall *f)
{
    uint32_t a=(uint32_t)f->a[0],b=(uint32_t)f->a[1],c=(uint32_t)f->a[2],result=0;
    uint32_t sp=LOCAL_BASE+0x400*h->depth;
    switch(f->function) {
    case 0x16F530: case 0x16F5D0: case 0x16F600: case 0x172860:
    case 0x17A800: case 0x17A8B0: case 0x17A970: case 0x17AAD0: case 0x17ABA0:
    case 0x17AF70: case 0x17B300: case 0x17B420: case 0x1B5DC0: {
        EmAimFireControl control={h,em_aim_fire_live_map,control_call,sp-0x10}; int32_t v0=0;
        TRY(em_aim_fire_control_run(&control,f->function,a,em_ee_word_int(b),f->f[0],&v0));
        if (f->function==0x17A800) f->f0=(uint32_t)v0;
        else f->v0=(uint32_t)v0;
        return 0;
    }
    case 0x17A130: case 0x17A0B0: case 0x179BC0: case 0x179CA0: {
        EmAimFirePose pose={h,em_aim_fire_live_map,pose_call,0};
        TRY(em_aim_fire_pose_run(&pose,f->function,a,b,c,f->f[0],&result)); f->v0=result; return 0;
    }
    case 0x185A10: case 0x185E30: case 0x199220: case 0x1854E0: case 0x185760:
    case 0x183AC0: case 0x183B80: {
        EmAimFireTarget target={0}; target.context=h; target.map=target_map; target.call=target_call; target.sp=sp;
        int status;
        switch(f->function) {
        case 0x185A10:status=em_aim_fire_target_00185A10(&target,a,&result);break;
        case 0x185E30:status=em_aim_fire_target_00185E30(&target,a,b,&result);break;
        case 0x199220:status=em_aim_fire_target_00199220(&target,a);break;
        case 0x1854E0:status=em_aim_fire_target_001854E0(&target,a);break;
        case 0x185760:status=em_aim_fire_target_00185760(&target,a);break;
        case 0x183AC0:status=em_aim_fire_target_00183AC0(&target,a,&result);break;
        default:status=em_aim_fire_target_00183B80(&target,a,&result);break;
        }
        if (status<0) {
            if (!h->fault_function) h->fault_function=target.fault_function;
            if (!h->fault_address) h->fault_address=target.fault_address;
            return -1;
        }
        f->v0=result;return 0;
    }
    case 0x18ABA0: {
        /* The round's impact marker, the pool behaviour 001861C0 stores
         * (em_aim_fire_runtime binds the record; AIM_FIRE.md section 7). */
        EmAimFireTarget target={0}; target.context=h; target.map=target_map; target.call=target_call; target.sp=sp;
        if (em_aim_fire_marker_0018ABA0(&target,a)<0) {
            if (!h->fault_function) h->fault_function=target.fault_function;
            if (!h->fault_address) h->fault_address=target.fault_address;
            return -1;
        }
        return 0;
    }
    case 0x170A60: case 0x171320: case 0x171670: case 0x171B00: case 0x171E90: case 0x1723D0:
        return machine(h,f->function,a,em_ee_word_int(b));
    case 0x1860A0: case 0x1861C0: case 0x1869A0: case 0x186A60: case 0x1872C0: case 0x187CC0: {
        EmAimFireShots shots={h,em_aim_fire_live_map,shots_call};int32_t v0=0;
        TRY(em_aim_fire_shots_run(&shots,f->function,a,b,&v0));
        if (f->function!=0x187CC0) f->v0=(uint32_t)v0;
        return 0;
    }
    default:return h->call ? h->call(h->context,f) : -1;
    }
}
int em_aim_fire_live_call(EmAimFireLive *h,EmAimFireTargetCall *f)
{
    if (!h || !f || h->fault_function || h->fault_address) return -1;
    if (h->depth>=16) { h->fault_function=f->function ? f->function : UINT32_MAX; return -1; }
    ++h->depth;
    if (!f->sp) f->sp=LOCAL_BASE+0x400*h->depth;
    int status=dispatch(h,f);
    --h->depth;
    if (status<0) {
        if (!h->fault_function) h->fault_function=f->function ? f->function : UINT32_MAX;
        return -1;
    }
    return h->fault_function || h->fault_address ? -1 : 0;
}
int em_aim_fire_live_clear_fault(EmAimFireLive *h)
{
    if (!h || h->depth) return -1;
    h->fault_function=h->fault_address=0;
    return 0;
}
