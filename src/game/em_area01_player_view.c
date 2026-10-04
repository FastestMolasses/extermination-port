#include "game/em_area01_player_view.h"
#include <string.h>

typedef struct { unsigned at, n; } Range;
/* Fields held in a different layout or by a different canonical owner. */
static const Range projected[]={{0xA0,0x20},{0xC4,4},{0x20E,2},{0x214,4},{0x220,16},{0x234,2},{0x308,4}};
/* Unbound callback/model/bone/link mutations must go through their native
 * owners between segments. Hip and homogeneous components are read-only. */
static const Range protected[]={{9,1},{0xC,1},{0x10,0x1C},{0x40,0x10},{0xAC,0x14},
                               {0xD0,0x120},{0x214,4},{0x308,4}};
static int fail(EmArea01PlayerView *v,int code,uint32_t at)
{
    if(v && !v->fault){v->fault=code;v->fault_address=at;}
    return -1;
}
void em_area01_player_view_init(EmArea01PlayerView *v,EmPlayerLiveActor *actor,
                               EmArea01PlayerSnapshot snapshot,EmArea01PlayerPublish publish,void *ctx)
{
    if(!v)return;
    memset(v,0,sizeof *v);v->actor=actor;v->snapshot=snapshot;v->publish=publish;v->ctx=ctx;
}
static void project(EmPlayerLiveActor *a,const EmArea01PlayerValues *s)
{
    memcpy(a->bytes+0xA0,s->position,12);em_live_set_u32(a,0xAC,0x3F800000);
    memcpy(a->bytes+0xB0,s->hip,12);em_live_set_u32(a,0xBC,0x3F800000);
    em_live_set_u32(a,0xC4,s->heading);memcpy(a->bytes+0x220,s->vitals,16);
    em_live_set_u16(a,0x20E,s->iframes);a->bytes[0x234]=s->infected;
    a->bytes[0x235]=(a->bytes[0x235]&~1u)|(s->low&1u);
    em_live_set_u32(a,0x214,s->link_owner);em_live_set_u32(a,0x308,s->link_prev);
}
int em_area01_player_view_begin(EmArea01PlayerView *v)
{
    if(!v || v->fault)return -1;
    if(v->active || !v->actor || !v->snapshot || !v->publish)return fail(v,1,EM_AREA01_PLAYER_BASE);
    memset(&v->owners,0,sizeof v->owners);
    if(v->snapshot(v->ctx,&v->owners)<0)return fail(v,2,EM_AREA01_PLAYER_BASE);
    memcpy(v->saved,v->actor->bytes,sizeof v->saved);
    v->link_owner=v->actor->link_owner;v->link_prev=v->actor->link_prev;
    v->link_flags=v->actor->link_flags;v->link_type=v->actor->link_type;
    project(v->actor,&v->owners);
    memcpy(v->before,v->actor->bytes,sizeof v->before);v->active=1;return 0;
}
int em_area01_player_view_borrow_begin(EmArea01PlayerView *v)
{
    if(!v || v->fault)return -1;
    if(v->active || !v->actor)return fail(v,1,EM_AREA01_PLAYER_BASE);
    memcpy(v->before,v->actor->bytes,sizeof v->before);
    v->link_owner=v->actor->link_owner;v->link_prev=v->actor->link_prev;
    v->link_flags=v->actor->link_flags;v->link_type=v->actor->link_type;
    v->active=2;return 0;
}
uint8_t *em_area01_player_view_bytes(EmArea01PlayerView *v,uint32_t a,uint32_t n,int write)
{
    if(a<EM_AREA01_PLAYER_BASE || a-EM_AREA01_PLAYER_BASE>=EM_PLAYER_ACTOR_SIZE)return NULL;
    if(!v || v->fault)return NULL;
    if(!v->active || !n || n>EM_PLAYER_ACTOR_SIZE || a-EM_AREA01_PLAYER_BASE>EM_PLAYER_ACTOR_SIZE-n){
        fail(v,1,a);return NULL;
    }
    unsigned off=a-EM_AREA01_PLAYER_BASE;
    if(write && v->active==2){fail(v,3,a);return NULL;}
    if(write)for(unsigned i=0;i<sizeof protected/sizeof *protected;i++)
        if(off<protected[i].at+protected[i].n && off+n>protected[i].at){fail(v,3,a);return NULL;}
    return v->actor->bytes+off;
}
int em_area01_player_view_commit(EmArea01PlayerView *v)
{
    EmArea01PlayerValues now;
    if(!v || v->fault)return -1;
    if(!v->active)return fail(v,1,EM_AREA01_PLAYER_BASE);
    if(v->active==2) {
        if(v->actor->link_owner!=v->link_owner || v->actor->link_prev!=v->link_prev ||
           v->actor->link_flags!=v->link_flags || v->actor->link_type!=v->link_type)
            return fail(v,2,EM_AREA01_PLAYER_BASE);
        if(memcmp(v->actor->bytes,v->before,sizeof v->before))
            return fail(v,3,EM_AREA01_PLAYER_BASE);
        v->active=0;return 0;
    }
    memset(&now,0,sizeof now);
    if(v->snapshot(v->ctx,&now)<0 || memcmp(&now,&v->owners,sizeof now) ||
       v->actor->link_owner!=v->link_owner || v->actor->link_prev!=v->link_prev ||
       v->actor->link_flags!=v->link_flags || v->actor->link_type!=v->link_type)
        return fail(v,2,EM_AREA01_PLAYER_BASE);
    for(unsigned i=0;i<sizeof protected/sizeof *protected;i++)
        if(memcmp(v->actor->bytes+protected[i].at,v->before+protected[i].at,protected[i].n))
            return fail(v,3,EM_AREA01_PLAYER_BASE+protected[i].at);
    uint8_t flags=v->actor->bytes[0x235]&~1u;
    v->publish(v->ctx,v->actor);
    /* Restore only the in-stage representation; all ordinary record stores
     * stay in the one actual record for the next worker/stage. */
    for(unsigned i=0;i<sizeof projected/sizeof *projected;i++)
        memcpy(v->actor->bytes+projected[i].at,v->saved+projected[i].at,projected[i].n);
    v->actor->bytes[0x235]=(v->actor->bytes[0x235]&1u)|flags;
    v->active=0;return 0;
}
