/* Binding contract checks for the actual live equipment owner. Original
 * equipment instruction equality has its own reference oracle. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/em_equipment_live.c"

static EmActorPool pool;
static EmActor *actor;
static Slot *slot;
static uint32_t address;
static unsigned checks, calls;
static int fail_call;
static EmPlayerLiveActor player;
static uint8_t player_matrix[64];
EmPlayerLiveActor *player_states_actor_mut(void) { return &player; }
const uint8_t *player_pose_record_bytes(uint32_t address,uint32_t size)
{ (void)address; return size==sizeof player_matrix ? player_matrix : NULL; }
#define CHECK(x) do { assert(x); ++checks; } while (0)
static uint32_t word(const void *p) { uint32_t v;memcpy(&v,p,4);return v; }
static void put(void *p,uint32_t v) { memcpy(p,&v,4); }
static void setup(void)
{
    memset(&S,0,sizeof S); memset(&pool,0,sizeof pool);
    S.pool=&pool;S.attached=1;actor=&pool.records[3];slot=&S.slot[3];
    actor->allocated=1;actor->generation=19;actor->callback=EM_PLAYER_EQUIPMENT_CALLBACK;
    actor->self=actor;actor->status=2;actor->u04[0]=1;actor->model=0;actor->param=0x15;
    slot->live=1;slot->generation=19;slot->n.self=&slot->n;
    slot->held=2;slot->word[0]=0x286400;slot->word[1]=0x286500;
    slot->n.bone[0]=&slot->bone[0];slot->n.bone[1]=&slot->bone[1];
    slot->model_address=0x234560;slot->n.h2E=0x1234;slot->n.vA0[0]=0xC0FFEE;
    address=em_actor_pool_address(&pool,actor);calls=0;
    header_import(slot);
}
static int boundary(uint32_t entry,uint32_t node)
{
    ++calls;CHECK(entry==0x1861C0 && node==address);
    CHECK(em_equipment_live_field(node,0x14,1)==actor);
    CHECK(actor->status==7 && actor->drawn==1 && actor->model==2);
    CHECK(actor->u04[0]==1 && actor->u04[1]==8 && actor->u04[3]==9);
    CHECK(actor->bones==2 && actor->u0A[2]==3 && actor->param==4);
    CHECK(em_equipment_live_field(node+0x2E,2,1)==&slot->n.h2E);
    CHECK(slot->n.h2E==0x1234 && word(em_equipment_live_field(node+0xA0,4,0))==0xC0FFEE);
    uint8_t *h=em_equipment_live_field(node,0x14,1);
    h[0]=3;h[1]=0;h[3]=4;h[4]=2;h[5]=6;h[7]=5;h[9]=1;h[12]=2;h[13]=10;
    uint16_t value=0;memcpy(em_equipment_live_field(node+0x2E,2,1),&value,2);
    put(em_equipment_live_field(node+0xC8,4,1),0x3F800000);
    put(em_equipment_live_field(node+0x214,4,1),0x87654321);
    CHECK(em_equipment_live_field(node+0x24,4,1)==NULL);
    return fail_call ? -1 : 0;
}
static void boundaries(void)
{
    for(fail_call=0;fail_call<2;++fail_call) {
        setup();S.aim_fire=boundary;
        EmPEN *n=&slot->n;
        n->status=7;n->drawn=1;n->flavour=2;n->state=1;n->b05=8;n->b07=9;
        n->bones_held=2;n->bone_count=3;n->variant=4;
        CHECK(aim_fire_node(n,0x1861C0)==-fail_call && calls==1);
        CHECK(n->status==3 && n->drawn==0 && n->flavour==4 && n->state==2);
        CHECK(n->b05==6 && n->b07==5 && n->bones_held==1 && n->bone_count==2 && n->variant==10);
        CHECK(n->h2E==0 && n->vC0[2]==0x3F800000 && n->w214==0x87654321);
        /* The next tick's import preserves the callback's completed stores. */
        header_import(slot);header_publish(slot);
        CHECK(n->state==2 && actor->u04[0]==2 && n->h2E==0 && n->w214==0x87654321);
        CHECK(n->self==n && slot->model_address==0x234560);
    }
}
static void tick_fault(void)
{
    setup();
    /* A caller writes through the original-address view before the next
     * actual live tick. A missing world owner then faults its preflight. */
    uint8_t *state=em_equipment_live_field(address+4,1,1);*state=2;
    uint8_t *event=em_equipment_live_field(address,1,1);*event=3;
    uint16_t pending=17;memcpy(em_equipment_live_field(address+0x2E,2,1),&pending,2);
    S.e=(EmPlayerEquipment){.world=&S.world,.workers=&S.workers,.fault=&S.efault};
    CHECK(em_equipment_live_tick(actor)<0);
    CHECK(S.fault!=0 && S.efault.address==0x8103C0);
    CHECK(slot->n.state==2 && actor->u04[0]==2 && slot->n.status==3 && actor->status==3);
    CHECK(slot->n.h2E==17 && *state==2 && *event==3);
    CHECK(em_equipment_live_field(address+0x2E,2,0)==NULL);
    CHECK(em_equipment_live_tick(actor)<0 && slot->n.h2E==17);
}
int main(void)
{
    setup();
    CHECK(em_equipment_live_field(address,EM_ACTOR_RECORD_SIZE,0)==NULL);
    CHECK(em_equipment_live_field(address,0x30,1)==NULL);
    const unsigned holes[]={0x18,0x20,0x24,0x2A,0x30,0x38,0x48,0x50,0x9C,0x200,0x218,0x2EF};
    for(unsigned i=0;i<sizeof holes/sizeof holes[0];++i) {
        CHECK(em_equipment_live_field(address+holes[i],1,0)==NULL);
        CHECK(em_equipment_live_field(address+holes[i],1,1)==NULL);
    }
    CHECK(em_equipment_live_field(address+0xA0,0x70,1)==slot->n.vA0);
    CHECK(em_equipment_live_field(address+0x210,8,1)==&slot->n.w210);
    CHECK(em_equipment_live_field(address+0xA0,0x71,1)==NULL);
    CHECK(em_equipment_live_field(address+0x210,9,1)==NULL);
    CHECK(word(em_equipment_live_field(address+0x14,4,0))==address);
    CHECK(em_equipment_live_field(address+0x14,4,1)==NULL);
    CHECK(word(em_equipment_live_field(address+0x44,4,0))==0x234560);
    CHECK(em_equipment_live_field(address+0x44,4,1)==NULL);
    CHECK(word(em_equipment_live_field(address+0x114,4,0))==slot->word[1]);
    CHECK(em_equipment_live_field(address+0x110,4,1)==NULL);
    CHECK(em_equipment_live_field(address+0x118,4,0)==NULL);
    CHECK(em_equipment_live_field(slot->word[0]+0x90,64,1)==slot->bone[0].world);
    CHECK(em_equipment_live_field(slot->word[0]+0x90,65,1)==NULL);
    CHECK(em_equipment_live_field(address,SIZE_MAX,0)==NULL);
    CHECK(em_equipment_live_field(address,0,0)==NULL);
    EmEquipmentLiveRegion regions[12];
    CHECK(em_equipment_live_regions(address,NULL,0)==10);
    CHECK(em_equipment_live_regions(address,regions,12)==10);
    for(unsigned i=0;i<10;++i) {
        CHECK(em_equipment_live_field(regions[i].address,regions[i].size,regions[i].writable)==regions[i].bytes);
        if(!regions[i].writable)CHECK(em_equipment_live_field(regions[i].address,regions[i].size,1)==NULL);
    }
    memset(regions,0xA5,sizeof regions);
    CHECK(em_equipment_live_regions(address,regions,1)==10);
    CHECK(regions[1].address==0xA5A5A5A5);
    actor->allocated=0;
    CHECK(em_equipment_live_field(address,1,0)==NULL);
    CHECK(em_equipment_live_field(slot->word[0]+0x90,16,1)==NULL);
    CHECK(em_equipment_live_regions(address,regions,12)==0);
    actor->allocated=1;++actor->generation;
    CHECK(em_equipment_live_field(address,1,0)==NULL);
    CHECK(em_equipment_live_regions(address,regions,12)==0);
    --actor->generation;actor->callback=0;
    CHECK(em_equipment_live_field(address,1,0)==NULL);
    CHECK(em_equipment_live_field(slot->word[0]+0x90,16,1)==NULL);
    boundaries();
    tick_fault();
    printf("aim/fire equipment live: PASS (%u checks)\n",checks);
    return 0;
}
