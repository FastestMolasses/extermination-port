#include "game/em_area01_model_draw.h"
#include "game/em_shadow_live.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int fail(EmArea01ModelDraw *d,uint32_t address)
{
    if(d && d->model && !d->model->fault) {
        d->model->fault=1;d->model->fault_address=address;
    }
    return -1;
}
static int call_fail(EmArea01ModelDraw *d,uint32_t address,int resumed)
{
    fail(d,address);
    EmArea01ActorView *v=d->model->actors;
    if(v && !v->fault) {
        if(resumed && !v->active)(void)em_area01_actor_view_begin(v);
        else if(!resumed && v->active)(void)em_area01_actor_view_commit(v);
    }
    return -1;
}
void em_area01_model_draw_bind(EmArea01ModelDraw *d,EmArea01Model *m)
{ if(d) { memset(d,0,sizeof *d);d->model=m; } }

static int prepare(EmArea01ModelDraw *d,uint32_t node,const uint8_t *r,int morph,int attachment)
{
    EmArea01Model *m=d->model;
    if(!m->source.resource)return fail(d,rd32(r+0x44));
    uint32_t handle=rd32(r+0x44);
    const uint8_t *head=m->source.resource(m->source.ctx,handle,0x40);
    if(!head)return fail(d,handle);
    uint32_t count=rd32(head+8),skeleton=rd32(head+0xC);
    if(!count || count>EM_OWNER_SERVICES_MAX_BONES || skeleton>0x1000000u)
        return fail(d,handle);
    uint32_t size=skeleton+count*0x50u;
    const uint8_t *bytes=m->source.resource(m->source.ctx,handle,size);
    memset(&d->bank,0,sizeof d->bank);
    const EmWorldModel *model=NULL;
    if(!bytes)return fail(d,handle);
    if(morph) {
        EmWorldModel *view=&d->bank.models[0];
        view->address=handle;view->bytes=bytes;view->size=size;
        view->model.bone_count=(uint8_t)count;memcpy(&view->model.radius,head+0x20,4);
        model=view;
    } else if(em_world_models_add(&d->bank,handle,bytes,size,&model)<0)return fail(d,handle);
    EmOwnerServicesOwner *o=&d->owner;memset(o,0,sizeof *o);
    o->drawn=r[1];o->cls=r[2];o->kind=r[3];o->bones_held=r[9];o->bone_count=r[0xC];
    o->model_id=r[0xD];o->model=&model->model;o->anim=rd32(r+0x40);
    o->attachment=rd32(r+0x90);memcpy(&o->collapsed_bone,r+0x94,2);o->pose_bone=r[0x98];
    memcpy(o->pos,r+0xB0,sizeof o->pos);memcpy(o->world,r+0xD0,sizeof o->world);
    count=o->bone_count;
    if(count>EM_OWNER_SERVICES_MAX_BONES)return fail(d,node+0xC);
    for(unsigned k=0;k<count;++k) {
        uint32_t address=rd32(r+0x110+4*k);
        d->nodes[k]=em_area01_model_slot_bytes(m,address,EM_ROGER_ACTOR_SLOT_BYTES);
        if(!d->nodes[k])return fail(d,address);
        memcpy(d->bones[k].world,d->nodes[k]+0x90,sizeof d->bones[k].world);
        o->bone[k]=&d->bones[k];
    }
    memcpy(d->rgb,r+0x80,sizeof d->rgb);d->record=node;
    d->regions[0]=(EmOwnerDrawLiveRegion){node,0x24,r};
    d->regions[1]=(EmOwnerDrawLiveRegion){node+0x28,EM_ACTOR_RECORD_SIZE-0x28,r+0x28};
    d->regions[2]=(EmOwnerDrawLiveRegion){m->actor.world.slots_base,m->actor.world.slots_size,m->actor.world.slots};
    d->region_count=3;
    if(morph) {
        d->regions[d->region_count++]=(EmOwnerDrawLiveRegion){handle,size,bytes};
    } else if(attachment && o->attachment) {
        const uint8_t *slot=em_area01_model_slot_bytes(m,o->attachment,EM_ROGER_ACTOR_SLOT_BYTES);
        if(!slot)return fail(d,o->attachment);
        uint32_t face=rd32(slot+0x60);
        head=m->source.resource(m->source.ctx,face,0x40);
        if(!head)return fail(d,face);
        size=0x40u+(rd32(head+4)&0xFFFFu)*16u;
        bytes=m->source.resource(m->source.ctx,face,size);
        if(!bytes)return fail(d,face);
        d->regions[d->region_count++]=(EmOwnerDrawLiveRegion){face,size,bytes};
    }
    return 0;
}

int em_area01_model_draw_call(EmArea01ModelDraw *d,uint32_t function,uint32_t node)
{
    if(!d || !d->model || d->model->fault)return -1;
    EmArea01Model *m=d->model;EmArea01ActorView *v=m->actors;
    int resumed=v->active;
    if(resumed && (em_area01_actor_view_touch(v,node)<0 ||
                   em_area01_actor_view_commit(v)<0))return call_fail(d,function,resumed);
    if(em_area01_actor_view_begin(v)<0)return call_fail(d,function,resumed);
    const uint8_t *r=em_area01_actor_view_bytes(v,node,0x24,0);
    if(!em_area01_actor_view_bytes(v,node+0x28,EM_ACTOR_RECORD_SIZE-0x28,0))r=NULL;
    int rc=-1;
    if(r && function==0x001DA6A0u) {
        unsigned count=r[0xC];
        if(count<=EM_OWNER_SERVICES_MAX_BONES) {
            rc=0;
            for(unsigned k=0;k<count;++k) {
                d->nodes[k]=em_area01_model_slot_bytes(m,rd32(r+0x110+4*k),EM_ROGER_ACTOR_SLOT_BYTES);
                if(!d->nodes[k])rc=-1;
            }
            if(!rc)rc=em_shadow_live_actor_001DA6A0(node,r,EM_ACTOR_RECORD_SIZE,d->nodes,count);
        }
    } else if(r && function==0x001CB360u && prepare(d,node,r,1,0)==0) {
        rc=em_owner_draw_live_001CB360(&d->owner,d->rgb,node,d->regions,d->region_count);
    } else if(r && function==0x001CAA00u && prepare(d,node,r,0,1)==0) {
        rc=em_owner_draw_live_001CAA00_attached(&d->bank,&d->owner,d->rgb,node,d->regions,d->region_count);
    } else if(r && function==0x001CACB0u && prepare(d,node,r,0,0)==0) {
        rc=em_owner_draw_live_001CABA0(&d->bank,&d->owner,d->rgb,node);
    }
    if(em_area01_actor_view_commit(v)<0)rc=-1;
    if(rc<0)return call_fail(d,function,resumed);
    if(resumed && em_area01_actor_view_begin(v)<0)return call_fail(d,function,resumed);
    return 0;
}
