/* Test-only composition of the actual collision world and sole SYS pair
 * owners. Capture storage is only an isolated oracle fixture. */
#include "area01_static_ground_bridge.c"
#include "game/em_area01_sys.h"
#include "game/em_area01_math_player.h"
#include "game/em_area01_collision_view.h"
#include "game/em_effect_original.h"
#include "game/em_ee_float.h"

static uint8_t *capture_ram, *capture_spad;
static EmArea01Sys pair_sys;
static uint32_t pair_calls[512][3], pair_count;
static uint32_t refused_address;
static int pair_refused;
static EmArea01CollisionView collision_view;
static EmAimFireWorldLive shared_world;
static uint64_t generation(void *ctx) { (void)ctx;return 1; }
static uint32_t identity_address(void *ctx,const void *p) { (void)ctx;return address(p); }
static const void *address_identity(void *ctx,uint32_t a) { (void)ctx;return actor(a); }

static uint8_t *capture_bytes(void *ctx,uint32_t a,uint32_t n)
{
    (void)ctx;
    if (!n || (refused_address && a<=refused_address && (uint64_t)a+n>refused_address)) return NULL;
    const EmCollSegment *seg=em_collision_world_segment();
    if (seg && a>=0x70003B86u && a+n<=0x70003B88u)
        return (uint8_t *)&seg->state->span_lo+a-0x70003B86u;
    if (seg && a>=0x70003B88u && a+n<=0x70003B8Au)
        return (uint8_t *)&seg->state->span_hi+a-0x70003B88u;
    if (a>=0x70000000u && a<=0x70004000u && n<=0x70004000u-a)
        return capture_spad+a-0x70000000u;
    return a<0x02000000u && n<=0x02000000u-a ? capture_ram+a : NULL;
}
static uint8_t *sys_bytes(void *ctx,uint32_t a,uint32_t n,int write)
{
    if (em_area01_collision_view_owns(&collision_view,a,n))
        return em_area01_collision_view_bytes(&collision_view,a,n,write);
    return capture_bytes(ctx,a,n);
}
static uint32_t owner_address(void *ctx,const EmActor *a)
{ (void)ctx;return address(a); }
static int sys_worker(void *ctx,EmArea01SysCall *c)
{
    (void)ctx;
    EmSdkMathContext *sdk=em_collision_world_sdk();
    float out=0;uint32_t fault=0;int rc;
    if (!sdk) return -1;
    switch(c->fn) {
    case 0x0011DF78u:out=em_sdk_math_original_0011DF78(em_ee_float(c->f[0]));rc=0;break;
    case 0x0011E748u:rc=em_sdk_math_original_0011E748(sdk->tables,&sdk->world,&sdk->workers,
                                                em_ee_float(c->f[0]),&out,&fault);break;
    case 0x0011E620u:rc=em_sdk_math_original_0011E620(sdk->tables,&sdk->world,&sdk->workers,
                             em_ee_float(c->f[0]),em_ee_float(c->f[1]),&out,&fault);break;
    case 0x0011E2A8u:rc=em_sdk_math_original_0011E2A8(sdk->tables,em_ee_float(c->f[0]),&out,&fault);break;
    case 0x0011DE90u:rc=em_sdk_math_original_0011DE90(sdk->tables,em_ee_float(c->f[0]),&out,&fault);break;
    case 0x001B1470u:rc=em_effect_original_001B1470(em_ee_float(c->f[0]),&out);break;
    case 0x00187EC0u: {
        EmA01Math math={.view=sys_bytes};
        return em_area01_math_00187EC0(&math,(uint32_t)c->a[0],(uint32_t)c->a[1]);
    }
    default:return -1;
    }
    c->f0=em_ee_bits(out);return rc<0 || fault ? -1 : 0;
}
static int pair(void *ctx,uint32_t fn,uint32_t a,uint32_t b)
{
    (void)ctx;
    if (pair_count>=512) return -1;
    pair_calls[pair_count][0]=fn;pair_calls[pair_count][1]=a;pair_calls[pair_count++][2]=b;
    if (pair_refused) return -1;
    if (em_area01_collision_view_begin(&collision_view)<0) return -1;
    int rc=-1;
    if (fn==0x001A8840u) rc=em_area01_sys_001A8840(&pair_sys,a,b);
    if (fn==0x001A9E00u) rc=em_area01_sys_001A9E00(&pair_sys,a,b);
    if (em_area01_collision_view_commit(&collision_view)<0) return -1;
    return rc;
}
int cg_bind(int mode)
{
    EmCollisionWorldAreaPasses b={NULL,capture_bytes,pair};
    if(mode==2)b.bytes=NULL;
    if(mode==3)b.pair=NULL;
    return em_collision_world_bind_area_passes(mode ? &b : NULL);
}
int cg_setup(uint8_t *ram,uint8_t *spad)
{
    capture_ram=ram;capture_spad=spad;pair_count=0;refused_address=0;pair_refused=0;
    memset(&pair_sys,0,sizeof pair_sys);pair_sys.view=sys_bytes;pair_sys.call=sys_worker;
    pair_sys.sp=0x7F080000u;
    EmActorCollisionWorld *world=em_collision_world_cells();
    const EmCollSegment *seg=em_collision_world_segment();
    if(!world || !seg)return -1;
    EmActorClassLists *lists=(EmActorClassLists *)(void *)world->lists;
    em_actor_class_lists_reset(lists);
    const uint32_t cursors[]={0x275BAC,0x275B9C,0x275B8C,0x275B7C,0x275B6C,0x275B5C};
    const uint32_t counts[]={0x275BB4,0x275BA4,0x275B94,0x275B84,0x275B74,0x275B64};
    for(unsigned k=0;k<EM_ACTOR_LIST_COUNT;++k) {
        unsigned count=half(ram+counts[k]);uint32_t at=word(ram+cursors[k]);
        if(count>EM_ACTOR_LIST_MAX || at>0x02000000u-4*count)return -1;
        for(unsigned i=0;i<count;++i) {
            EmActor *a=actor(word(ram+at+4*i));if(!a)return -1;
            lists->list[k].slot[count-1-i]=a;
        }
        lists->list[k].live=(int16_t)count;
    }
    seg->state->span_lo=(int16_t)half(spad+0x3B86);
    seg->state->span_hi=(int16_t)half(spad+0x3B88);
    EmCollisionWorldOwners owners={.address_of=owner_address};
    em_collision_world_bind_owners(&owners);
    memset(&shared_world,0,sizeof shared_world);
    EmArea01CollisionHost host={.shared=&shared_world,.segment=seg,
        .move=em_collision_world_move_scratch(),.address=identity_address,
        .identity=address_identity,.generation=generation,.bytes=sys_bytes};
    host.file.address=word(ram+0x28A598);host.file.bytes=ram+host.file.address;
    host.file.size=0x02000000u-host.file.address;
    host.file.d28A5A8=word(ram+0x28A5A8);
    host.file.d28A5A8_value=half(ram+host.file.d28A5A8);
    if(em_area01_collision_view_bind(&collision_view,&host)<0)return -1;
    return cg_bind(1);
}
int cg_run(uint32_t *fault)
{
    EmSceneState scene={0};
    scene.d810700=capture_ram[0x810700];scene.d810702=capture_ram[0x810702];
    scene.spad3B8D=capture_spad[0x3B8D];
    scene.progress.bytes[0x81070A-EM_SCENE_PROGRESS_BASE]=capture_ram[0x81070A];
    if(em_area01_collision_view_begin(&collision_view)<0 ||
       em_area01_collision_view_commit(&collision_view)<0)return -1;
    int rc=em_collision_world_close_out_001AAD00(&scene,(int16_t)half(capture_ram+0x28A9A0),fault);
    if(em_area01_collision_view_begin(&collision_view)<0 ||
       em_area01_collision_view_commit(&collision_view)<0)return -1;
    return rc;
}
uint32_t cg_count(void) { return pair_count; }
const uint32_t *cg_calls(void) { return pair_calls[0]; }
uint16_t cg_span(unsigned i)
{
    const EmCollSegment *seg=em_collision_world_segment();
    return seg ? (uint16_t)(i ? seg->state->span_hi : seg->state->span_lo) : 0;
}
int cg_published(unsigned i)
{
    EmActorCollisionWorld *world=em_collision_world_cells();
    return world && i<EM_ACTOR_LIST_COUNT ? world->lists->list[i].published : -1;
}
void cg_refuse(uint32_t address,int worker) { refused_address=address;pair_refused=worker; }
