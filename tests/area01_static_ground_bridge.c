/* Fixture adapter for the live collision world's placement-kind binding. */
#include "game/em_collision_world.h"
#include <string.h>

typedef struct {
    int kind, record, poly;
    uint32_t entity;
    uint16_t node;
    uint8_t class_known;
    float point[3], delta[3], normal[3], grid_start[3], grid_end[3];
} GroundHit;

static EmCollision fixture_grid;
static EmActor fixture_actors[257];
static uint32_t word(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint16_t half(const uint8_t *p) { uint16_t v; memcpy(&v,p,2); return v; }
static EmActor *actor(uint32_t a)
{
    if (a==0x008102B0u) return &fixture_actors[256];
    if (a<EM_ACTOR_POOL_BASE || (a-EM_ACTOR_POOL_BASE)%EM_ACTOR_RECORD_SIZE ||
        (a-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE>=256) return NULL;
    return &fixture_actors[(a-EM_ACTOR_POOL_BASE)/EM_ACTOR_RECORD_SIZE];
}
static uint32_t address(const EmActor *a)
{
    if (!a) return 0;
    for (unsigned i=0;i<257;++i) if (a==&fixture_actors[i])
        return i==256 ? 0x008102B0u : EM_ACTOR_POOL_BASE+i*EM_ACTOR_RECORD_SIZE;
    return UINT32_MAX;
}

int sg_load(const char *emcl,const char *cells)
{
    em_collision_world_unload(); em_collision_free(&fixture_grid);
    if (em_collision_load(&fixture_grid,emcl)<0) return -1;
    return em_collision_world_load(&fixture_grid,emcl,cells,EM_COLLISION_WORLD_SDK_PATH);
}
int sg_capture(const uint8_t *ram,const uint8_t *spad,uint32_t size)
{
    EmActorCollisionWorld *world=em_collision_world_cells();
    if (!world || !ram || !spad || world->table->size!=size) return -1;
    uint32_t table=word(spad+0x3250);
    if (table>0x02000000u || size>0x02000000u-table) return -1;
    memcpy(world->table->bytes,ram+table,size);
    memset(fixture_actors,0,sizeof fixture_actors);
    for (unsigned i=0;i<257;++i) {
        uint32_t at=i==256 ? 0x008102B0u : EM_ACTOR_POOL_BASE+i*EM_ACTOR_RECORD_SIZE;
        EmActor *a=&fixture_actors[i];
        a->status=ram[at]; a->cls=ram[at+2]; a->uid=half(ram+at+0xE);
        a->kind=half(ram+at+0x54); a->param=ram[at+0xD]; a->self=a;
    }
    /* The fixture copies the captured published list into the live world's
     * sole list owner. No close-out/contact pass is part of this query. */
    EmActorClassLists *lists=(EmActorClassLists *)(void *)world->lists;
    em_actor_class_lists_reset(lists);
    unsigned count=half(ram+0x275B84);uint32_t cursor=word(ram+0x275B7C);
    if (count>EM_ACTOR_LIST_MAX || cursor>0x02000000u-4u*count) return -1;
    EmActorClassList *list=&lists->list[EM_ACTOR_LIST_CLASS4];
    for (unsigned i=0;i<count;++i) {
        EmActor *a=actor(word(ram+cursor+4*i));if (!a) return -1;
        list->slot[count-1-i]=a;
    }
    list->published=(int16_t)count;
    return 0;
}
int sg_ground(uint32_t self,uint8_t cls,const float *point,const float *probe,
              uint32_t mask,float *feet,GroundHit *out)
{
    EmActorCollisionQuery q={actor(self),cls,feet};
    EmActorCollisionHit h;
    int r=em_actor_collision_ground_0019AB20(em_collision_world_cells(),&q,point,probe,mask,&h);
    if (r<0) return r;
    out->kind=h.kind;out->record=h.record;out->poly=h.poly;
    out->entity=address(h.entity);out->node=h.node;out->class_known=h.node_class_known;
    memcpy(out->point,h.point,12);memcpy(out->delta,h.delta,12);memcpy(out->normal,h.normal,12);
    memcpy(out->grid_start,h.grid_start,12);memcpy(out->grid_end,h.grid_end,12);
    return r;
}
int sg_kind(unsigned i)
{
    const EmActorCollisionWorld *world=em_collision_world_cells();
    return world && world->static_kind && i<world->static_kind_count ? world->static_kind[i] : -1;
}
