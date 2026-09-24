#include "game/em_interaction_scene.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t u16(const unsigned char *p) { return (uint16_t)(p[0]|p[1]<<8); }
static float f32(const unsigned char *p)
{ uint32_t value=u32(p);float result;memcpy(&result,&value,4);return result; }
static int fail(EmInteractionScene *scene,const char *message)
{ scene->count=0;snprintf(scene->error,sizeof scene->error,"%s",message);return 0; }

int em_interaction_scene_load(EmInteractionScene *scene,const char *path)
{
    if (!scene) return 0;
    memset(scene,0,sizeof *scene);
    if (!path) return fail(scene,"Missing EMIS path");
    FILE *file=fopen(path,"rb");
    if (!file) return fail(scene,"Cannot open required EMIS owner metadata");
    unsigned char bytes[96+80*EM_INTERACTION_CAPACITY+1];
    size_t size=fread(bytes,1,sizeof bytes,file);
    int read_error=ferror(file);fclose(file);
    if (read_error || size<96 || memcmp(bytes,"EMIS",4) || u32(bytes+4)!=1 ||
        u32(bytes+8)!=0xb00 || u32(bytes+16)!=80)
        return fail(scene,"Invalid EMIS header/version/AREA11 key");
    uint32_t count=u32(bytes+12);
    if (count!=11 || count>EM_INTERACTION_CAPACITY || size!=96+80*count)
        return fail(scene,"Invalid EMIS owner count or exact file size");
    float math[19];
    for (unsigned i=0;i<19;++i) {
        math[i]=f32(bytes+20+4*i);
        if (!isfinite(math[i])) return fail(scene,"Nonfinite EMIS SDK coefficient");
    }
    memcpy(&scene->math,math,sizeof math);
    for (unsigned i=0;i<count;++i) {
        const unsigned char *p=bytes+96+80*i;
        EmInteractionSceneOwner *o=&scene->owners[i];
        o->source_id=u32(p);o->role=u32(p+4);o->publication_rank=u32(p+8);
        o->callback=u32(p+12);o->item_type=u32(p+16);o->uid=u16(p+20);
        o->initial_status=p[22];o->class_flags=p[23];o->subtype=p[24];o->selector=p[25];
        if (!o->source_id || o->role>EM_INTERACTION_ROGER || u16(p+26) || u32(p+28) ||
            !(o->class_flags&0x80) || !(o->initial_status&1) || o->uid>>8!=11 ||
            (i && o->publication_rank<=scene->owners[i-1].publication_rank))
            return fail(scene,"Invalid EMIS identity/role/publication metadata");
        for (unsigned j=0;j<i;++j) {
            const EmInteractionSceneOwner *prior=&scene->owners[j];
            if (prior->source_id==o->source_id ||
                (o->role==EM_INTERACTION_PICKUP && prior->role==o->role && prior->uid==o->uid) ||
                (o->role!=EM_INTERACTION_PICKUP && prior->role==o->role))
                return fail(scene,"Duplicate EMIS canonical owner/role/UID");
        }
        unsigned kind=o->class_flags&0x1f;
        int valid=0;
        switch (o->role) {
        case EM_INTERACTION_PICKUP:
            valid=(kind==4 || kind==7) && (o->selector==3 || o->selector==4) &&
                (o->callback==0x219550 || o->callback==0x15afa0 || o->callback==0x15b030);break;
        case EM_INTERACTION_PANEL:valid=kind==4 && o->subtype==0x24 && !o->selector && o->callback==0x159210;break;
        case EM_INTERACTION_ELEVATOR:valid=kind==4 && o->selector==1 && o->callback==0x827b10;break;
        case EM_INTERACTION_DOOR:valid=kind==5 && !o->selector && o->callback==0x1bc350;break;
        case EM_INTERACTION_ROGER:valid=kind==10 && !o->selector && o->callback==0x8237e0;break;
        }
        if (!valid) return fail(scene,"EMIS role disagrees with original owner predicate");
        float values[12];
        for (unsigned j=0;j<12;++j) {
            values[j]=f32(p+32+4*j);
            if (!isfinite(values[j])) return fail(scene,"Nonfinite EMIS owner geometry");
        }
        memcpy(o->descriptor,values,24);memcpy(o->position,values+6,12);memcpy(o->angles,values+9,12);
        unsigned radius=o->role==EM_INTERACTION_ELEVATOR ? 3 : 0;
        if (!(o->descriptor[radius]>0) || o->descriptor[radius+1]<0)
            return fail(scene,"Invalid EMIS interaction radius/height");
    }
    unsigned roles[5]={0};
    for (unsigned i=0;i<count;++i) ++roles[scene->owners[i].role];
    if (roles[0]!=7 || roles[1]!=1 || roles[2]!=1 || roles[3]!=1 || roles[4]!=1)
        return fail(scene,"Incomplete initial AREA11 owner set");
    scene->count=count;
    return 1;
}

EmInteractionSceneOwner *em_interaction_scene_find(EmInteractionScene *scene,uint32_t source_id)
{
    if (scene) for (size_t i=0;i<scene->count;++i)
        if (scene->owners[i].source_id==source_id) return &scene->owners[i];
    return NULL;
}

EmInteractionSceneOwner *em_interaction_scene_pickup(EmInteractionScene *scene,uint16_t uid)
{
    if (scene) for (size_t i=0;i<scene->count;++i)
        if (scene->owners[i].role==EM_INTERACTION_PICKUP && scene->owners[i].uid==uid)
            return &scene->owners[i];
    return NULL;
}

EmInteractionSceneOwner *em_interaction_scene_role(EmInteractionScene *scene,EmInteractionRole role)
{
    if (!scene || role==EM_INTERACTION_PICKUP) return NULL;
    for (size_t i=0;i<scene->count;++i)
        if (scene->owners[i].role==(uint32_t)role) return &scene->owners[i];
    return NULL;
}

int em_interaction_scene_bind(EmInteractionScene *scene,uint32_t source_id,void *native_owner,
                              uint8_t *status,uint8_t *class_flags,uint8_t *armed)
{
    EmInteractionSceneOwner *owner=em_interaction_scene_find(scene,source_id);
    if (!owner || !status || !class_flags || !armed) return 0;
    owner->native_owner=native_owner;owner->live_status=status;
    owner->live_class_flags=class_flags;owner->live_armed=armed;
    return 1;
}

static int candidate(EmInteractionSceneOwner *owner,EmInteractionCandidate *out)
{
    if (!owner || !owner->live_status || !owner->live_class_flags || !owner->live_armed) return 0;
    *out=(EmInteractionCandidate){owner,*owner->live_status,*owner->live_class_flags,owner->live_armed};
    return 1;
}

int em_interaction_scene_scan(EmInteractionScene *scene,EmInteractionScanState *state,
                              EmInteractionPredicate predicate,void *context,size_t *winner_index)
{
    if (!scene || !state) return -1;
    if (state->selector || state->fade_wait || state->inhibited)
        return em_interaction_scan(state,NULL,0,NULL,NULL,winner_index);
    for (size_t i=0;i<scene->list.active_count;++i) {
        EmInteractionCandidate *entry=&scene->list.active[i];
        if (!candidate(entry->owner,entry)) return -1;
    }
    return em_interaction_scan(state,scene->list.active,scene->list.active_count,
                               predicate,context,winner_index);
}

typedef struct {
    EmInteractionPredicate predicate;
    void *context;
    int failed;
} CheckedPredicate;

static int checked_predicate(void *context, const EmInteractionCandidate *entry, float *score)
{
    CheckedPredicate *checked = context;
    if (checked->failed) return 0;
    int result = checked->predicate(checked->context, entry, score);
    if (result < 0) {
        checked->failed = 1;
        return 0;
    }
    return result;
}

int em_interaction_scene_scan_checked(EmInteractionScene *scene, EmInteractionScanState *state,
    EmInteractionPredicate predicate, void *context, size_t *winner_index)
{
    if (winner_index) *winner_index = SIZE_MAX;
    if (!scene || !state) return -1;
    if (state->selector || state->fade_wait || state->inhibited) return 0;
    const size_t count = scene->list.active_count;
    if (count > EM_INTERACTION_CAPACITY || (count && !predicate)) return -1;
    EmInteractionCandidate entries[EM_INTERACTION_CAPACITY];
    uint8_t arms[EM_INTERACTION_CAPACITY];
    for (size_t i = 0; i < count; ++i) {
        if (!candidate(scene->list.active[i].owner, &entries[i])) return -1;
        arms[i] = *entries[i].armed;
        entries[i].armed = &arms[i];
    }
    EmInteractionScanState next = *state;
    CheckedPredicate checked = {predicate, context, 0};
    size_t winner;
    int result = em_interaction_scan(&next, entries, count, checked_predicate, &checked, &winner);
    if (result < 0 || checked.failed) return -1;
    if (result) {
        EmInteractionSceneOwner *owner = entries[winner].owner;
        *owner->live_armed = arms[winner];
    }
    *state = next;
    if (winner_index) *winner_index = winner;
    return result;
}
