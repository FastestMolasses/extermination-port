#include "game/em_area01_model_live.h"
#include "game/em_area01_math_actor.h"
#include "game/em_area11_boxes.h"
#include "game/em_owner_services_original.h"
#include "game/em_render_verify_rest.h"
#include "game/em_anim_runtime_rest.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p)
{ uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p)
{ uint16_t v; memcpy(&v, p, 2); return v; }
static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static int fail(EmArea01Model *m, uint32_t address)
{ if (m && !m->fault) { m->fault = 1; m->fault_address = address; } return -1; }

uint8_t *em_area01_model_slot_bytes(EmArea01Model *m, uint32_t address, uint32_t size)
{
    if (!m || !size || m->fault) return NULL;
    EmRogerActorWorld *w = &m->actor.world;
    if (!w->slots || address < w->slots_base || address - w->slots_base > w->slots_size ||
        size > w->slots_size - (address - w->slots_base)) return NULL;
    return w->slots + (address - w->slots_base);
}

/* These are serialization views only. The actor adapter keeps the original
 * bytes not represented by EmActor; no persistent model state is copied. */
static void typed_load(EmArea01Model *m)
{
    const uint8_t *r = m->record;
    EmRogerActorRecord *t = &m->typed;
    memset(t, 0, sizeof *t);
    t->address = m->node;
    t->status=r[0]; t->drawn=r[1]; t->cls=r[2]; t->lifecycle=r[4];
    t->bones_held=r[9]; t->bone_count=r[0xC]; t->kind=r[0xD];
    t->w14=rd32(r+0x14); t->parent=rd32(r+0x18); t->descriptor=rd32(r+0x30);
    t->anim=rd32(r+0x40); t->model=rd32(r+0x44); t->draw=rd32(r+0x4C);
    t->face_active=(int16_t)rd16(r+0x56); t->w58=rd32(r+0x58);
    t->face=rd32(r+0x90); t->face_bone=(int16_t)rd16(r+0x94);
    t->shadow_kind=(int16_t)rd16(r+0x96); t->pose_bone=r[0x98];
    memcpy(t->fA0,r+0xA0,16); memcpy(t->fB0,r+0xB0,16); memcpy(t->fC0,r+0xC0,16);
    memcpy(t->bone,r+0x110,sizeof t->bone);
}
static void typed_store(EmArea01Model *m)
{
    uint8_t *r=m->record; const EmRogerActorRecord *t=&m->typed;
    r[0]=t->status; r[1]=t->drawn; r[2]=t->cls; r[4]=t->lifecycle;
    r[9]=t->bones_held; r[0xC]=t->bone_count; r[0xD]=t->kind;
    wr32(r+0x30,t->descriptor); wr32(r+0x40,t->anim); wr32(r+0x44,t->model);
    wr32(r+0x4C,t->draw); wr16(r+0x56,(uint16_t)t->face_active); wr32(r+0x58,t->w58);
    wr32(r+0x90,t->face); wr16(r+0x94,(uint16_t)t->face_bone);
    wr16(r+0x96,(uint16_t)t->shadow_kind); r[0x98]=t->pose_bone;
    memcpy(r+0xA0,t->fA0,16); memcpy(r+0xB0,t->fB0,16); memcpy(r+0xC0,t->fC0,16);
    memcpy(r+0x110,t->bone,sizeof t->bone);
}
static int refresh(EmArea01Model *m)
{
    m->record=em_area01_actor_view_bytes(m->actors,m->node,0x24,0);
    if (!em_area01_actor_view_bytes(m->actors,m->node+0x28,EM_ACTOR_RECORD_SIZE-0x28,0)) return -1;
    if (!m->record) return -1;
    typed_load(m);
    m->actor.world.d00275B40=(const uint32_t *)(const void *)(m->record+0x110);
    m->actor.world.d00275B40_count=EM_ROGER_ACTOR_MAX_BONES;
    return 0;
}
static int external(EmArea01Model *m, uint32_t function, uint32_t node, uint32_t arg)
{
    if (!m->source.worker) return -1;
    typed_store(m);
    if (em_area01_actor_view_commit(m->actors)<0) return -1;
    int rc=m->source.worker(m->source.ctx,function,node,arg);
    if (em_area01_actor_view_begin(m->actors)<0 || refresh(m)<0) return -1;
    return rc;
}
static int head(void *ctx,uint32_t actor,int32_t kind)
{ return external(ctx,0x001F0120u,actor,kind); }
static int shadow(void *ctx,EmRogerActorRecord *actor)
{ return external(ctx,0x001DA6A0u,actor->address,0); }
static int alternative(void *ctx,EmRogerActorRecord *actor)
{ return external(ctx,0x001BA7F0u,actor->address,0); }
static int setup(void *ctx,uint8_t count)
{
    EmArea01Model *m=ctx; (void)count;
    EmAnimRest rest={0};rest.world.d275B48=m->source.current_actor;
    rest.world.d275B40=m->source.current_bones;
    return em_anim_rest_001CB5B0(&rest);
}
static int face_tick(void *ctx,EmRogerActorRecord *actor)
{
    EmArea01Model *m=ctx;
    uint8_t *slot=em_area01_model_slot_bytes(m,actor->face,EM_ROGER_ACTOR_SLOT_BYTES);
    if (!slot || !m->source.random) return -1;
    em_opening_face_tick_slot(slot,m->source.random,m->source.ctx);
    return 0;
}
static int equipment_bind(void *,EmRogerActorRecord *,uint32_t,int32_t,int32_t,int32_t *);
static int equipment_draw(void *ctx,EmRogerActorRecord *actor)
{ return external(ctx,actor->draw,actor->address,0); }
static int equipment_free(void *,EmRogerActorRecord *);

int em_area01_model_bind(EmArea01Model *m,EmArea01ActorView *actors,const EmArea01ModelSource *source)
{
    if (!m || !actors || !actors->pool || !source) return -1;
    memset(m,0,sizeof *m); m->actors=actors; m->source=*source;
    const EmRogerActorWorld *slots=em_area11_boxes_slot_world();
    if (!slots) return fail(m,0x001AF710u);
    m->actor.world=*slots;
    EmRogerActorWorld *w=&m->actor.world;
    w->d0028A490=source->table; w->d0028A490_count=source->table_words;
    w->resource=source->resource; w->resource_ctx=source->ctx;
    w->d00810758=source->d810758; w->d00810788=source->d810788;
    w->d00810700=source->d810700; w->d008106D4=source->d8106D4;
    w->spad3600=source->pose_globals ? source->pose_globals->spad3600 : NULL;
    m->actor.workers.ctx=m;
    m->actor.workers.w_001CB5B0=setup;
    m->actor.workers.w_001F0120=head;
    m->actor.workers.w_001DA6A0=shadow; m->actor.workers.w_001BA7F0=alternative;
    m->actor.workers.w_001D0720=face_tick;
    m->actor.workers.w_001B1020=equipment_bind;
    m->actor.workers.w_draw=equipment_draw;
    m->actor.workers.w_001AFC10=equipment_free;
    return 0;
}

static int pose_bind(EmArea01Model *m, int needs_bank)
{
    const EmArea01ModelSource *s=&m->source;
    EmPoseHost *h=&m->pose; memset(h,0,sizeof *h);
    EmRogerActorWorld *w=&m->actor.world;
    h->region[h->region_count++]=(EmPoseRegion){w->slots_base,w->slots_size,w->slots,1};
    h->region[h->region_count++]=(EmPoseRegion){m->node,EM_ACTOR_RECORD_SIZE,m->record,1};
    if (needs_bank) {
        if (!s->resource_rest) return -1;
        uint32_t bank=rd32(m->record+0x40), size=0;
        const uint8_t *bytes=s->resource_rest(s->ctx,bank,&size);
        if (!bytes || !size) return -1;
        h->region[h->region_count++]=(EmPoseRegion){bank,size,(uint8_t *)(void *)bytes,0};
    }
    h->globals=s->pose_globals;
    m->stage.d8106F1=s->d8106F1; m->stage.d810CB6=s->d810CB6;
    m->stage_globals.d810707=s->d810707;
    m->advance.stage=&m->stage; m->advance.globals=&m->stage_globals;
    EmPlayerStageCallees *c=&m->advance.callees; memset(c,0,sizeof *c);
    c->context=h; c->bone_init=em_pose_host_stage_bone_init; c->clip_init=em_pose_host_stage_clip_init;
    c->clip_resolve=em_pose_host_stage_clip_resolve; c->skeleton_frame=em_pose_host_stage_skeleton_frame;
    c->w001C8710=em_pose_host_stage_8710; c->w001C87C0=em_pose_host_stage_87C0;
    c->sample_bones=em_pose_host_stage_sample_bones; c->request=em_pose_host_stage_request;
    h->callees.advance_context=&m->advance; h->callees.advance=em_pose_host_player_advance;
    return 0;
}

/* C69A0 operates on the original byte layout. In particular, raw slot
 * writes made by an overlay since the previous typed draw stay visible.
 * The caller already owns an active actor-view segment; these four pure
 * math leaves never cross a native actor boundary. */
static uint8_t *pose_math_bytes(void *ctx,uint32_t address,uint32_t size,int write)
{
    EmArea01Model *m=ctx;
    if (!size || m->fault) return NULL;
    if (!write && m->record && address>=m->node &&
        address-m->node<=EM_ACTOR_RECORD_SIZE &&
        size<=EM_ACTOR_RECORD_SIZE-(address-m->node))
        return m->record+(address-m->node);
    uint8_t *slot=em_area01_model_slot_bytes(m,address,size);
    if (slot) return slot;
    EmPoseGlobals *g=m->source.pose_globals;
    const struct { uint32_t address,size; uint32_t *bytes; } spans[]={
        {0x70003400u,64,g ? g->spad3400 : NULL},
        {0x70003440u,64,g ? g->spad3440 : NULL},
        {0x70003480u,64,m->source.scratch3480},
        {0x70003600u,16,g ? g->spad3600 : NULL},
        {0x70003760u,44,g ? g->spad3760 : NULL}
    };
    for (unsigned i=0;i<sizeof spans/sizeof spans[0];++i)
        if (spans[i].bytes && address>=spans[i].address &&
            address-spans[i].address<=spans[i].size &&
            size<=spans[i].size-(address-spans[i].address))
            return (uint8_t *)(void *)spans[i].bytes+(address-spans[i].address);
    fail(m,address);
    return NULL;
}
static int pose_math_worker(void *ctx,uint32_t fn,const uint32_t *a,unsigned na,
                            const uint32_t *f,unsigned nf,uint32_t *v0,uint32_t *f0)
{
    EmArea01Model *m=ctx; (void)v0; (void)f0;
    if (fn==0x001CA0A0u && na==3 && nf==1) {
        uint8_t *out=pose_math_bytes(m,a[0],16,1);
        const uint8_t *left=pose_math_bytes(m,a[1],16,0);
        const uint8_t *right=pose_math_bytes(m,a[2],16,0);
        uint32_t q[4],l[4],r[4];
        if (!out || !left || !right) return -1;
        memcpy(l,left,16);memcpy(r,right,16);
        em_pose_host_001CA0A0(q,l,r,f[0]);memcpy(out,q,16);return 0;
    }
    if (fn==0x001CA1C0u && na==3 && nf==0) {
        uint8_t *out=pose_math_bytes(m,a[0],64,1);
        const uint8_t *quat=pose_math_bytes(m,a[1],16,0);
        const uint8_t *translation=pose_math_bytes(m,a[2],12,0);
        uint8_t *scratch=pose_math_bytes(m,0x70003760u,44,1);
        uint32_t matrix[16],q[4],t[3],s[11];
        if (!out || !quat || !translation || !scratch) return -1;
        memcpy(q,quat,16);memcpy(t,translation,12);memcpy(s,scratch,44);
        em_pose_host_001CA1C0(matrix,q,t,s);
        memcpy(scratch,s,44);memcpy(out,matrix,64);return 0;
    }
    if (fn==0x001029C0u && na==1 && nf==0) {
        uint8_t *out=pose_math_bytes(m,a[0],64,1);float matrix[16];
        if (!out || em_owner_services_identity_001029C0(matrix)<0) return -1;
        memcpy(out,matrix,64);return 0;
    }
    if (fn==0x00102C58u && na==3 && nf==0) {
        uint8_t *out=pose_math_bytes(m,a[0],64,1);
        const uint8_t *in=pose_math_bytes(m,a[1],64,0);
        const uint8_t *angles=pose_math_bytes(m,a[2],12,0);
        float matrix[16],rotation[3];
        if (!out || !in || !angles) return -1;
        memcpy(matrix,in,64);memcpy(rotation,angles,12);
        if (em_owner_services_euler_00102C58(matrix,matrix,rotation)<0) return -1;
        memcpy(out,matrix,64);return 0;
    }
    return fail(m,fn);
}
static int animated_pose(EmArea01Model *m)
{
    EmA01Math math={0};math.ctx=m;math.view=pose_math_bytes;math.call=pose_math_worker;
    if (em_area01_math_001C69A0(&math,m->node)<0)
        return fail(m,math.fault_address);
    return 0;
}

/* Default bone initialization uses only temporary layout views. Its sole
 * implementation is em_owner_services_001C62C0; results return to the same
 * original-layout slot records the pose host will read next. */
static int default_bones(EmArea01Model *m)
{
    unsigned n=m->record[0xC];
    if (n>EM_OWNER_SERVICES_MAX_BONES || !m->source.resource) return -1;
    uint32_t model=rd32(m->record+0x44);
    const uint8_t *head=m->source.resource(m->source.ctx,model,0x10);
    if (!head) return -1;
    uint32_t skeleton=rd32(head+0xC);
    const uint8_t *raw=m->source.resource(m->source.ctx,model+skeleton,n*0x50u);
    if (n && !raw) return -1;
    EmOwnerSkeletonRecord records[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerBone bones[EM_OWNER_SERVICES_MAX_BONES];
    uint8_t *slots[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerModel mod={0}; mod.bone_count=head[8]; mod.skeleton=records; mod.skeleton_records=n;
    EmOwnerServicesOwner owner={0}; owner.model=&mod; owner.bone_count=(uint8_t)n;
    for (unsigned k=0;k<n;++k) {
        slots[k]=em_area01_model_slot_bytes(m,rd32(m->record+0x110+4*k),EM_POSE_NODE_BYTES);
        if (!slots[k]) return -1;
        records[k].parent=(int16_t)rd16(raw+k*0x50+4);
        memcpy(records[k].bind,raw+k*0x50+0x10,64);
        memset(&bones[k],0,sizeof bones[k]); owner.bone[k]=&bones[k];
    }
    EmOwnerServices services={0};
    if (em_owner_services_001C62C0(&services,&owner)<0) return -1;
    for (unsigned k=0;k<n;++k) {
        memcpy(slots[k],bones[k].bind,64); wr16(slots[k]+0x64,(uint16_t)bones[k].parent);
        memcpy(slots[k]+0x70,bones[k].rot,12); memcpy(slots[k]+0x7C,bones[k].trans,12);
        memcpy(slots[k]+0x88,bones[k].scale,6);
    }
    return 0;
}

/* A call-local layout projection for the shared model services. Allocations
 * still pop the existing boxes stack; original record and slot bytes remain
 * authoritative. This also serves library models without a Roger instance. */
typedef struct {
    EmArea01Model *model;
    EmOwnerServices services;
    EmOwnerServicesOwner owner;
    EmOwnerModel resource;
    EmOwnerSkeletonRecord skeleton[EM_OWNER_SERVICES_MAX_BONES];
    EmOwnerBone bone[EM_OWNER_SERVICES_MAX_BONES];
    uint8_t *slot[EM_OWNER_SERVICES_MAX_BONES];
    uint32_t address[EM_OWNER_SERVICES_MAX_BONES];
    unsigned slots, allocated;
} ModelServices;
static void service_bone(EmOwnerBone *b,uint8_t *raw,int store)
{
#define FIELD(member,offset,size) do { \
    if (store) memcpy(raw+offset,b->member,size); \
    else memcpy(b->member,raw+offset,size); \
} while (0)
    FIELD(bind,0,64); FIELD(rot,0x70,12); FIELD(trans,0x7C,12);
    FIELD(scale,0x88,6); FIELD(world,0x90,64);
#undef FIELD
    if (store) memcpy(raw+0x64,&b->parent,2);
    else memcpy(&b->parent,raw+0x64,2);
}
static int service_resource(ModelServices *v,uint32_t address)
{
    EmArea01Model *m=v->model;
    const uint8_t *p=m->source.resource ? m->source.resource(m->source.ctx,address,0x10) : NULL;
    if (!p) return fail(m,address);
    unsigned count=p[8];
    if (count>EM_OWNER_SERVICES_MAX_BONES) return -1;
    uint32_t skeleton=rd32(p+0xC);
    const uint8_t *raw=m->source.resource(m->source.ctx,address+skeleton,count*0x50u);
    if (count && !raw) return fail(m,address+skeleton);
    v->resource.bone_count=(uint8_t)count;
    v->resource.skeleton=v->skeleton;v->resource.skeleton_records=count;
    for (unsigned k=0;k<count;++k) {
        v->skeleton[k].parent=(int16_t)rd16(raw+k*0x50+4);
        memcpy(v->skeleton[k].bind,raw+k*0x50+0x10,64);
    }
    v->owner.model=&v->resource;
    return 0;
}
static int service_lookup(void *ctx,uint32_t bank,uint32_t id,uint32_t *result)
{
    ModelServices *v=ctx; EmArea01Model *m=v->model;
    uint32_t address=bank+4u+4u*(id&0x7FFFu);
    const uint8_t *word=m->source.resource ? m->source.resource(m->source.ctx,address,4) : NULL;
    if (!word) return fail(m,address);
    EmPoseHost host={0};
    host.region[0]=(EmPoseRegion){address,4,(uint8_t *)(void *)word,0};host.region_count=1;
    return em_pose_host_001C6120(&host,bank,(int)id,result);
}
/* Indicator binds use the same original C2360/C22A0 owner as the older
 * typed indicator host. Here its record and slots are already canonical
 * original-layout bytes; no persistent Child model view is created. */
static int indicator_lookup(void *ctx,uint32_t bank,uint32_t id,uint32_t *out)
{ ModelServices v={0};v.model=ctx;return service_lookup(&v,bank,id,out); }
static int indicator_model(void *ctx,uint8_t *self,uint32_t node,uint32_t model,int32_t kind)
{
    EmArea01Model *m=ctx;uint32_t method=rd32(self+0x4C),old=rd32(self+0x44);
    if(self!=m->record || node!=m->node)return -1;
    int rc=em_owner_services_model_001CA5E0(&old,&method,model,(uint32_t)kind);
    wr32(self+0x44,old);wr32(self+0x4C,method);return rc;
}
static int indicator_count(void *ctx,uint32_t model,uint32_t *out)
{
    EmArea01Model *m=ctx;uint8_t count=0;
    int rc=em_roger_actor_001C6150(&m->actor,model,&count);*out=count;return rc;
}
static int indicator_pop(void *ctx,uint32_t *out)
{ return em_roger_actor_001AF780(&((EmArea01Model *)ctx)->actor,out); }
static int indicator_setup(void *ctx,int32_t count)
{ return setup(ctx,(uint8_t)count); }
static int indicator_default(void *ctx,uint8_t *self,uint32_t node)
{
    EmArea01Model *m=ctx;
    return self==m->record && node==m->node ? default_bones(m) : -1;
}
static int indicator_bind(EmArea01Model *m,uint32_t fn)
{
    const uint32_t *bank=fn==0x001C2360u ?
        (m->source.library_word ? m->source.library_word :
         (m->source.table_words>0x37 ? m->source.table+0x37 : NULL)) :
        (m->source.table_words>0x43 ? m->source.table+0x43 : NULL);
    if(!bank || !m->actor.world.d00275BCC)return -1;
    const EmRvrModelWorkers workers={m,indicator_lookup,indicator_model,indicator_count,
        indicator_pop,indicator_setup,indicator_default};
    EmRvrFault fault={0};int32_t result=0;
    int rc=fn==0x001C2360u ?
        em_rvr_001C2360(m->record,EM_ACTOR_RECORD_SIZE,m->node,*bank,*m->actor.world.d00275BCC,
                         &workers,&result,&fault) :
        em_rvr_001C22A0(m->record,EM_ACTOR_RECORD_SIZE,m->node,*bank,*m->actor.world.d00275BCC,
                         &workers,&result,&fault);
    return rc<0 ? fail(m,fault.address) : result;
}
static int service_model(void *ctx,EmOwnerServicesOwner *owner,uint32_t address)
{
    ModelServices *v=ctx;
    if (owner!=&v->owner || em_roger_actor_001CA6E0(&v->model->actor,&v->model->typed,address)<0)
        return -1;
    return service_resource(v,address);
}
static int service_pop(void *ctx,EmOwnerBone **result)
{
    ModelServices *v=ctx;unsigned k=v->allocated;
    if (k>=EM_OWNER_SERVICES_MAX_BONES) return -1;
    if (em_roger_actor_001AF780(&v->model->actor,&v->address[k])<0) return -1;
    ++v->allocated;v->slots=v->allocated;
    if (!v->address[k]) { *result=NULL;return 0; }
    v->slot[k]=em_area01_model_slot_bytes(v->model,v->address[k],EM_POSE_NODE_BYTES);
    if (!v->slot[k]) return -1;
    service_bone(&v->bone[k],v->slot[k],0);*result=&v->bone[k];
    return 0;
}
static int service_setup(void *ctx,uint8_t count)
{ return setup(((ModelServices *)ctx)->model,count); }
static void service_store(ModelServices *v,int binding)
{
    EmArea01Model *m=v->model;uint8_t *r=m->record;EmOwnerServicesOwner *o=&v->owner;
    if (binding) {
        r[4]=o->lifecycle;r[9]=o->bones_held;r[0xC]=o->bone_count;
        wr32(r+0x40,o->anim);wr32(r+0x44,m->typed.model);wr32(r+0x4C,m->typed.draw);
        for (unsigned k=0;k<v->allocated;++k) wr32(r+0x110+4*k,v->address[k]);
    } else memcpy(r+0xD0,o->world,64);
    for (unsigned k=0;k<v->slots;++k) if (v->slot[k]) service_bone(&v->bone[k],v->slot[k],1);
}
static int service_clip(void *ctx,EmOwnerServicesOwner *owner,int16_t clip)
{
    ModelServices *v=ctx;EmArea01Model *m=v->model;
    if (owner!=&v->owner) return -1;
    service_store(v,1);
    if (pose_bind(m,1)<0 || em_pose_host_001C63E0(&m->pose,m->record,EM_ACTOR_RECORD_SIZE,clip)<0) return -1;
    for (unsigned k=0;k<v->slots;++k) if (v->slot[k]) service_bone(&v->bone[k],v->slot[k],0);
    return 0;
}
static int service_call(EmArea01Model *m,uint32_t fn,uint32_t a1,int32_t a2,int32_t a3)
{
    ModelServices v={0};v.model=m;EmOwnerServicesOwner *o=&v.owner;uint8_t *r=m->record;
    o->lifecycle=r[4];o->bones_held=r[9];o->bone_count=r[0xC];o->anim=rd32(r+0x40);
    memcpy(o->scale,r+0x60,16);memcpy(o->pos,r+0xB0,16);memcpy(o->rot,r+0xC0,16);
    memcpy(o->world,r+0xD0,64);
    v.services.world.d0028A56C=m->source.library_word ? m->source.library_word :
        (m->source.table_words>0x37 ? m->source.table+0x37 : NULL);
    v.services.world.d0028A490=m->source.table;
    v.services.world.d0028A490_count=m->source.table_words;
    v.services.world.d00275BCC=m->actor.world.d00275BCC;
    /* C6380/C9610 access only s3400. Borrow the canonical matrix directly. */
    v.services.world.scratch=m->source.pose_globals ?
        (EmOwnerServicesScratch *)(void *)m->source.pose_globals->spad3400 : NULL;
    v.services.workers.ctx=&v;v.services.workers.w_001C6120=service_lookup;
    v.services.workers.w_001CA6E0=service_model;v.services.workers.w_001AF780=service_pop;
    v.services.workers.w_anim_bone_array_setup=service_setup;
    v.services.workers.w_bone_init_default_2=service_clip;
    int binding=fn==0x001B1020u;
    if (!binding) {
        if (o->bone_count>EM_OWNER_SERVICES_MAX_BONES) return -1;
        v.slots=o->bone_count;
        for (unsigned k=0;k<v.slots;++k) {
            v.address[k]=rd32(r+0x110+4*k);
            v.slot[k]=em_area01_model_slot_bytes(m,v.address[k],EM_POSE_NODE_BYTES);
            if (!v.slot[k]) return -1;
            service_bone(&v.bone[k],v.slot[k],0);o->bone[k]=&v.bone[k];
        }
    }
    int rc=binding ? em_owner_services_001B1020(&v.services,o,a1,a2,a3)
                   : em_owner_services_001C6380(&v.services,o);
    service_store(&v,binding);
    return rc;
}

static EmActor *pool_actor(EmArea01Model *m,uint32_t node)
{
    if (node<EM_ACTOR_POOL_BASE) return NULL;
    uint32_t offset=node-EM_ACTOR_POOL_BASE;
    if (offset%EM_ACTOR_RECORD_SIZE || offset/EM_ACTOR_RECORD_SIZE>=EM_ACTOR_POOL_CAPACITY) return NULL;
    EmActor *a=&m->actors->pool->records[offset/EM_ACTOR_RECORD_SIZE];
    return a->allocated && a->self==a ? a : NULL;
}

static int equipment_bind(void *ctx,EmRogerActorRecord *actor,uint32_t a1,int32_t a2,int32_t a3,int32_t *out)
{
    EmArea01Model *m=ctx;
    if(actor!=&m->typed)return -1;
    typed_store(m);
    int rc=service_call(m,0x001B1020u,a1,a2,a3);
    typed_load(m);*out=rc;return rc<0 ? -1 : 0;
}
static int equipment(EmArea01Model *m)
{
    EmRogerActorRecord parent={0};
    if(m->typed.lifecycle<=1) {
        uint32_t node=m->node;uint8_t *record=m->record;EmRogerActorRecord child=m->typed;
        EmActor *native=pool_actor(m,child.parent);
        if(!native)return fail(m,child.parent);
        m->node=child.parent;
        int rc=refresh(m);parent=m->typed;
        m->node=node;m->record=record;m->typed=child;
        m->actor.world.d00275B40=(const uint32_t *)(const void *)(record+0x110);
        if(rc<0)return fail(m,child.parent);
    }
    if(m->typed.lifecycle==1 && parent.lifecycle<2 && parent.bones_held) {
        if(!m->source.current_bones)return fail(m,0x00275B40u);
        uint32_t array=*m->source.current_bones;
        uint8_t *word=em_area01_actor_view_bytes(m->actors,array,4,0);
        if(!word)return fail(m,array);
        m->actor.world.d00275B40=(const uint32_t *)(const void *)word;
        m->actor.world.d00275B40_count=1;
    }
    return em_roger_actor_001C5C90(&m->actor,&m->typed,m->typed.lifecycle<=1?&parent:NULL);
}

int em_area01_model_release_prepare(EmArea01Model *m,uint32_t node,EmArea01ModelRelease *r)
{
    if (!m || !r || m->fault || !m->actors || !m->actors->active) return -1;
    memset(r,0,sizeof *r);
    EmActor *a=pool_actor(m,node);
    if (!a || em_area01_actor_view_touch(m->actors,node)<0) return fail(m,node);
    unsigned index=(node-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE;
    EmArea01ActorRecordView *v=&m->actors->record[index];
    unsigned count=v->image[9];
    if (count>EM_ROGER_ACTOR_MAX_BONES) return fail(m,node+9);
    /* The generic path may publish only private words. A typed shared
     * owner must release through its existing service instead. */
    for (unsigned i=0;i<count*4u;++i)
        if (v->shared[0x110+i]) return fail(m,node+0x110+i);
    r->model=m;r->actor=a;r->generation=a->generation;r->index=index;
    r->typed.address=node;r->typed.bones_held=(uint8_t)count;r->typed.bone_count=v->image[0xC];
    memcpy(r->typed.bone,v->image+0x110,count*4u);
    r->ready=1;
    return 0;
}

int em_area01_model_release_native(EmArea01ModelRelease *r,EmActor *a)
{
    if (!r || !r->ready || !r->model) return -1;
    EmArea01Model *m=r->model;r->ready=0;
    if (m->fault || !m->actors || m->actors->active || a!=r->actor || !a ||
        !a->allocated || a->self || a->generation!=r->generation ||
        a!=&m->actors->pool->records[r->index]) return fail(m,0x001AF800u);
    EmArea01ActorRecordView *v=&m->actors->record[r->index];
    unsigned count=r->typed.bones_held;
    if (a->bones!=count || a->u0A[2]!=r->typed.bone_count ||
        memcmp(v->image+0x110,r->typed.bone,count*4u)) return fail(m,r->typed.address+0x110);
    int rc=em_roger_actor_001AF800(&m->actor,&r->typed);
    /* +110 is private canonical storage, retained across pool free/reuse.
     * Do not begin a record transaction after the pool cleared +14. */
    memcpy(v->image+0x110,r->typed.bone,count*4u);
    a->bones=r->typed.bones_held;a->u0A[2]=r->typed.bone_count;
    v->image[9]=a->bones;v->image[0xC]=a->u0A[2];
    return rc<0 ? fail(m,m->actor.fault.address) : 0;
}

static int equipment_release(void *ctx,EmActor *a)
{ return em_area01_model_release_native(ctx,a); }
static int equipment_free(void *ctx,EmRogerActorRecord *actor)
{
    EmArea01Model *m=ctx;EmArea01ModelRelease release;
    if(actor!=&m->typed || !m->source.worker)return -1;
    typed_store(m);
    if(em_area01_model_release_prepare(m,actor->address,&release)<0 ||
       em_area01_actor_view_commit(m->actors)<0)return -1;
    EmActorPool *pool=m->actors->pool;
    EmActorBoneRelease previous=pool->w_001AF800;void *previous_ctx=pool->worker_ctx;
    pool->w_001AF800=equipment_release;pool->worker_ctx=&release;
    int rc=m->source.worker(m->source.ctx,0x001AFC10u,actor->address,0);
    pool->w_001AF800=previous;pool->worker_ctx=previous_ctx;
    if(em_area01_actor_view_begin(m->actors)<0)return -1;
    if(pool_actor(m,actor->address)) {
        if(refresh(m)<0)return -1;
        return rc<0 ? rc : fail(m,0x001AFC10u);
    }
    /* The original free owns these bytes now; no post-free actor access. */
    m->record=NULL;return rc;
}

static int call_fault(EmArea01Model *m,uint32_t address,int resumed)
{
    fail(m,address);
    /* A worker failure does not itself invalidate the actor transaction.
     * Restore the caller's segment state so it can publish prior writes
     * without replacing the reached failure with a double-commit fault. */
    if (m->actors && !m->actors->fault) {
        if (resumed && !m->actors->active) (void)em_area01_actor_view_begin(m->actors);
        else if (!resumed && m->actors->active) (void)em_area01_actor_view_commit(m->actors);
    }
    return -1;
}

int em_area01_model_call(EmArea01Model *m,uint32_t fn,uint32_t node,uint32_t a1,uint32_t a2,
                          uint32_t a3,float f12,float f13,uint32_t *result)
{
    if (!m || !result || m->fault) return -1;
    *result=0;
    int resumed=m->actors->active;
    EmActor *native=pool_actor(m,node);
    if (resumed && native && em_area01_actor_view_touch(m->actors,node)<0)
        return call_fault(m,fn,resumed);
    if (resumed && em_area01_actor_view_commit(m->actors)<0) return call_fault(m,fn,resumed);
    if (fn==0x001B0FD0u || fn==0x001B0EA0u) {
        if (!native) return call_fault(m,fn,resumed);
        /* +40 and +D0 are not written by either original bind. Preserve
         * their canonical raw values when this record first gains a typed
         * boxes service view (pool reuse does not clear these bytes). */
        if (em_area01_actor_view_begin(m->actors)<0) return call_fault(m,fn,resumed);
        m->node=node;
        if (refresh(m)<0) return call_fault(m,fn,resumed);
        uint32_t anim=rd32(m->record+0x40);
        float world[16]; memcpy(world,m->record+0xD0,sizeof world);
        if (em_area01_actor_view_commit(m->actors)<0) return call_fault(m,fn,resumed);
        int32_t value=0;
        int rc=fn==0x001B0FD0u ? em_area11_boxes_owner_001B0FD0(native,m->actors->pool,&value)
                                : em_area11_boxes_owner_001B0EA0(native,m->actors->pool,&value);
        EmOwnerServicesOwner *view=em_area11_boxes_owner_view(native);
        if (view) { view->anim=anim; memcpy(view->world,world,sizeof world); }
        /* The older typed owner binder addresses its array directly. This
         * byte-addressed caller also publishes 001CB5B0's canonical word. */
        if (rc>=0 && value==0 && setup(m,native->u0A[2])<0) rc=-1;
        if (em_area11_boxes_owner_sync_slots(native,0)<0) rc=-1;
        if (em_area01_actor_view_begin(m->actors)<0) return call_fault(m,fn,resumed);
        m->node=node;
        if (refresh(m)<0 || em_area01_actor_view_commit(m->actors)<0) rc=-1;
        if (rc<0) return call_fault(m,fn,resumed);
        *result=(uint32_t)value;
        if (resumed && em_area01_actor_view_begin(m->actors)<0) return call_fault(m,fn,resumed);
        return 0;
    }
    if (em_area01_actor_view_begin(m->actors)<0) return call_fault(m,fn,resumed);
    int rc=0, store=0;
    if (fn==0x001AF890u) rc=em_roger_actor_001AF890(&m->actor,node);
    else if (fn==0x001AF780u) rc=em_roger_actor_001AF780(&m->actor,result);
    else if (fn==0x001C6150u) {
        uint8_t count=0; rc=em_roger_actor_001C6150(&m->actor,node,&count); *result=count;
    } else if (fn==0x001CB5B0u) rc=setup(m,(uint8_t)node);
    else {
        m->node=node;
        /* FD0/EA0 export their initial typed slots at bind. Thereafter raw
         * slots are canonical for these generic owners: publishing the
         * typed snapshot here would discard direct parent/overlay stores. */
        if (refresh(m)<0) rc=-1;
        else {
            EmRogerActorRecord *t=&m->typed;
            switch(fn) {
            case 0x001C5C90u: rc=equipment(m); store=1; break;
            case 0x001C2360u: case 0x001C22A0u: rc=indicator_bind(m,fn); break;
            case 0x001B1020u: case 0x001C6380u: rc=service_call(m,fn,a1,(int32_t)a2,(int32_t)a3); break;
            case 0x001B10B0u: rc=em_roger_actor_001B10B0(&m->actor,t,a1,(int32_t)a2); store=1; break;
            case 0x001CA5E0u: rc=em_owner_services_model_001CA5E0(&t->model,&t->draw,a1,a2); store=1; break;
            case 0x001CA6E0u: rc=em_roger_actor_001CA6E0(&m->actor,t,a1); store=1; break;
            case 0x001CA6F0u: rc=em_roger_actor_001CA6F0(&m->actor,t,a1); store=1; break;
            case 0x001AF800u: rc=em_roger_actor_001AF800(&m->actor,t); store=1; break;
            case 0x001BA8E0u: rc=em_roger_actor_001BA8E0(&m->actor,t,a1); store=1; break;
            case 0x001BA580u: rc=em_roger_actor_001BA580(&m->actor,t,a1); store=1; break;
            case 0x001BA540u: rc=em_roger_actor_001BA540(&m->actor,t); store=1; break;
            case 0x001CA700u: rc=em_roger_actor_001CA700(&m->actor,t,a1,(int32_t)a2); store=1; break;
            case 0x001CA770u: rc=em_roger_actor_001CA770(&m->actor,t); store=1; break;
            case 0x001D06D0u: rc=em_roger_actor_001D06D0(&m->actor,t,a1); break;
            case 0x001D06E0u: rc=em_roger_actor_001D06E0(&m->actor,t,a1); break;
            case 0x001D0C70u: rc=face_tick(m,t); break;
            case 0x001D8BF0u: rc=em_roger_actor_001D8BF0(&m->actor,t,(int32_t)a1); store=1; break;
            case 0x001C62C0u: rc=default_bones(m); break;
            case 0x001C69A0u: rc=animated_pose(m); break;
            case 0x001C63E0u: case 0x001C67E0u: case 0x001C68C0u: case 0x001C64F0u:
                rc=pose_bind(m,fn!=0x001C68C0u);
                if (rc<0) break;
                if (fn==0x001C63E0u) rc=em_pose_host_001C63E0(&m->pose,m->record,EM_ACTOR_RECORD_SIZE,(int32_t)a1);
                else if (fn==0x001C67E0u) rc=em_pose_host_001C67E0(&m->pose,m->record,EM_ACTOR_RECORD_SIZE,(int32_t)a1,f12,f13);
                else if (fn==0x001C68C0u) rc=em_pose_host_001C68C0(&m->pose,m->record,EM_ACTOR_RECORD_SIZE);
                else rc=em_player_stage_anim_advance(&m->advance,(EmPlayerLiveActor *)(void *)m->record,f12,result);
                break;
            default: rc=-1; break;
            }
            if (store && m->record) typed_store(m);
            if (m->record && em_area11_boxes_owner_sync_slots(native,1)<0) rc=-1;
            if (rc>=0 && fn!=0x001C64F0u) *result=(uint32_t)rc;
        }
    }
    if (em_area01_actor_view_commit(m->actors)<0) rc=-1;
    if (rc<0) return call_fault(m,m->actor.fault.address ? m->actor.fault.address : fn,resumed);
    if (resumed && em_area01_actor_view_begin(m->actors)<0) return call_fault(m,fn,resumed);
    return 0;
}
