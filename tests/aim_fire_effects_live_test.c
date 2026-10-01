/* ASan contract checks for the effects composition APIs. Synthetic tables
 * are test inputs. Body equality uses the existing original-instruction
 * effect/head-sprite/kinds oracles. Including the binder exposes its private
 * fixture state without adding a production injection API. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/game/em_effects_live.c"

EmGameState g;
static EmActorPool pool;
static EmSceneState scene;
static EmPlayerLiveActor player;
static uint8_t context_bytes[EM_RCL_CONTEXT_SIZE], matrices[0x100], arena[0x10000], chain_table[0x8000];
static uint8_t cable_sources[0x1B0], field_index[2];
static EmPacketChain pc;
static EmPacketChainRegion regions[3];
static int bound, called, unknown_matrix_resolves;
EmPlayerLiveActor *player_states_actor_mut(void) { return &player; }
EmPoseHost *player_pose_record_host(void) { return NULL; }
const uint8_t *em_area11_roger_record_bytes(uint32_t a,uint32_t n) { (void)a;(void)n;return NULL; }
const uint8_t *em_area11_roger_slot_bytes(uint32_t a,uint32_t n) { (void)a;(void)n;return NULL; }
uint8_t *em_frame_d810E80(void) { return field_index; }
const uint8_t *em_rcl_bytes(uint32_t a,uint32_t n)
{
    if(a>=CTX && n<=sizeof context_bytes && a-CTX<=sizeof context_bytes-n)return context_bytes+(a-CTX);
    if(a>=0x70003A40 && n<=sizeof matrices && a-0x70003A40<=sizeof matrices-n)return matrices+(a-0x70003A40);
    ++unknown_matrix_resolves;
    return NULL;
}
const uint8_t *em_aim_fire_tables_bytes(uint32_t a,uint32_t n)
{
    return a>=0x266930 && n<=sizeof cable_sources && a-0x266930<=sizeof cable_sources-n ? cable_sources+(a-0x266930):NULL;
}
static void write32(uint8_t *p,uint32_t v) { memcpy(p,&v,4); }
static int binder(EmActor *a) { assert(a->allocated);bound++;return 0; }
static int abs_worker(void *c,uint32_t in,uint32_t *out) { (void)c;*out=in&0x7FFFFFFF;return 0; }
static void setup(void)
{
    memset(&S,0,sizeof S);memset(&pool,0,sizeof pool);memset(&scene,0,sizeof scene);
    memset(context_bytes,0,sizeof context_bytes);memset(matrices,0,sizeof matrices);
    memset(arena,0,sizeof arena);memset(chain_table,0,sizeof chain_table);memset(cable_sources,0x39,sizeof cable_sources);
    em_actor_pool_reset_001AF8E0(&pool);bound=called=unknown_matrix_resolves=0;
    S.loaded=S.attached=1;S.pool=&pool;S.scene=&scene;S.bind=binder;
    S.etables.global=EM_EFFECT_ORIGINAL_TABLE_BASE;
    S.etables.bytes[0]=0x0C;S.etables.bytes[8]=7;
    write32(S.etables.bytes+0xC,0x21AAC0);write32(S.etables.bytes+0x24,0xFFFFFFFF);
    S.eworkers.w_001AFA90=w_001AFA90;
    S.e=(EmEffectOriginal){&S.etables,&S.eglobals,&S.decals,&S.eview,&S.eworkers,{0,0}};
    S.kworkers.w_0011DF78=abs_worker;
    S.k=(EmEffectKinds){&S.ktables,&S.kglobals,&S.decals,&S.particles,&S.kworkers,{0,0}};
    S.xs.d275670=CTX;
    for(unsigned i=0;i<4;++i) {
        write32(context_bytes+0x2240+i*20,0x3F800000);
        write32(context_bytes+0x22C0+i*20,0x3F800000);
        write32(matrices+i*20,0x3F800000);write32(matrices+0x80+i*20,0x3F800000);
    }
    write32(context_bytes+0xA0,0x437F0000);write32(context_bytes+0xA8,0x437F0000);
    write32(context_bytes+0x18,0x300000);
    regions[0]=(EmPacketChainRegion){CTX,sizeof context_bytes,context_bytes};
    regions[1]=(EmPacketChainRegion){0x300000,sizeof arena,arena};
    regions[2]=(EmPacketChainRegion){0x7635C0,sizeof chain_table,chain_table};
    em_packet_chain_init(&pc,regions,3,CTX,0x814220);
    S.hworkers.ctx=&pc;S.hworkers.w_001CD370=w_head_001CD370;
    S.hworkers.w_001CB5F0=em_packet_chain_w_001CB5F0;
    S.hworkers.w_001CB6B0=em_packet_chain_w_001CB6B0;
    S.hworkers.w_001CB760=em_packet_chain_w_001CB760_4;
    S.hworkers.w_001CB900=em_packet_chain_w_001CB900;
    S.hworld.scratch_3A40=matrices;S.hworld.scratch_3AC0=matrices+0x80;
    S.hworld.ctx_A0=context_bytes+0xA0;S.hworld.tables=&S.htables;
    write32(cable_sources+0x8C,1);
}
static int other_tick(void *ctx,uint32_t address,uint32_t callback)
{
    assert(ctx==&called && callback==0x21AAC0);called++;
    uint8_t *p=em_effects_live_node_field(address+5,1,1);assert(p);*p=19;
    uint32_t *scratch=em_effects_live_node_field(address+0x1F0,4,1);assert(scratch);*scratch=0xA5B6C7D8;
    return 1;
}
static void spawn_and_fields(void)
{
    setup();uint32_t address=0xDEADBEEF;
    assert(em_effects_live_001EF9D0(0x80000000,NULL,0x3F800000,&address)==0);
    assert(address==EM_ACTOR_POOL_BASE && bound==1);
    assert(em_effects_live_node_field(address+0x24,4,0)==NULL);
    assert(em_effects_live_node_field(address+0x25,1,1)==NULL);
    EmEffectsLiveNodeRegion spans[40];
    size_t count=em_effects_live_node_regions(address,spans,40);assert(count>0 && count<40);
    for(size_t i=0;i<count;++i) {
        assert(spans[i].address!=address+0x24);
        assert(spans[i].bytes==em_effects_live_node_field(spans[i].address,spans[i].size,0));
    }
    uint32_t *parent=em_effects_live_node_field(address+0x24,4,1);assert(parent);
    assert(em_effects_live_node_field(address+0x24,4,0)==NULL);
    assert(em_effects_live_node_regions(address,NULL,0)==count);
    *parent=0x8102B0;
    assert(em_effects_live_node_field(address+0x24,4,0)==NULL);
    assert(em_effects_live_node_written(address+0x25,1)==-1);
    assert(em_effects_live_node_written(address+0x24,4)==0);
    assert(parent==em_effects_live_node_field(address+0x24,4,0) && *parent==0x8102B0);
    assert(em_effects_live_node_regions(address,spans,40)==count+1);
    assert(spans[1].address==address+0x24 && spans[1].bytes==parent && spans[1].size==4);
    assert(em_effects_live_node_regions(address+1,spans,40)==0);
    assert(em_effects_live_node_field(address+0x20,4,0)==NULL);
    assert(em_effects_live_node_field(address+0xB0,16,1)==pool.records[0].pos);
    assert(em_effects_live_node_field(address+0xC0,16,1)==pool.records[0].rot);
    assert(em_effects_live_node_field(address+0xD0,64,1)==S.slot[0].e.matrix);
    assert(em_effects_live_node_field(address+0x1F0,0x100,1)==pool.records[0].scratch);
    assert(em_effects_live_node_field(address+0x1F0,0x101,1)==NULL);
    assert(em_effects_live_node_field(address+0xBC,8,1)==NULL);
    assert(em_effects_live_node_field(address+0x14,4,0)==NULL);
    assert(em_effects_live_set_other_tick(other_tick,&called)==0);
    assert(em_effects_live_tick(&pool.records[0])==1 && called==1);
    assert(pool.records[0].u04[1]==19 && rd32(pool.records[0].scratch)==0xA5B6C7D8);
    assert(em_actor_pool_free_001AFC10(&pool,&scene,&pool.records[0])==0);
    assert(em_effects_live_node_field(address+0x24,4,0)==NULL);
    assert(em_effects_live_node_written(address+0x24,4)==-1);
    assert(em_effects_live_node_regions(address,spans,40)==0);
    assert(em_effects_live_001EF9D0(0x80000000,NULL,0x3F800000,&address)==0);
    assert(em_effects_live_node_field(address+0x24,4,0)==NULL);
    setup();float pos[4]={1,2,3,7},rot[4]={.5f,.25f,-.5f,1};
    assert(em_effects_live_001EFD90_result(0x80000000,pos,rot,&address)==0);
    assert(address==EM_ACTOR_POOL_BASE && bound==1);
    assert(memcmp(pool.records[0].pos,pos,12)==0 && pool.records[0].pos[3]==1);
    assert(memcmp(pool.records[0].rot,rot,16)==0);
    setup();write32(S.etables.bytes+0xC,0);address=7;
    assert(em_effects_live_001EF9D0(0x80000000,NULL,0x3F800000,&address)==0 && address==0 && bound==0);
}
static void transforms(void)
{
    setup();float matrix[16]={1,0,0,0,0,1,0,0,0,0,1,0,2,3,.5f,1};
    uint8_t block[0x60];memset(block,0xA5,sizeof block);
    assert(em_effects_live_001CFA60(block,matrix,0x3F000000,0x3E800000)==0);
    assert(unknown_matrix_resolves==0);
    assert(memcmp(block,matrix,64)==0 && rd32(block+0x40)==CTX+0x2240);
    assert(rd32(block+0x44)==0x3F000000 && rd32(block+0x4C)==0x3E800000);
    assert(rd32(block+0x48)==0x3F800000 && rd32(block+0x54)==0);
    for(unsigned i=0x58;i<0x60;++i)assert(block[i]==0xA5);
    assert(em_effects_live_001CFBE0(0x8000,1,0x266930,block,0)==0 && S.counters.chains==1);
    uint32_t f[5]={0x3E000000,0x3E800000,0x3F000000,0x3F400000,0x3E800000};
    assert(em_effects_live_001CFB50(block,0,matrix,f)==0);
    assert(rd32(block+0x44)==f[0] && rd32(block+0x4C)==f[1] && rd32(block+0x48)==f[2] && rd32(block+0x50)==f[3]);
    for(unsigned i=0x58;i<0x60;++i)assert(block[i]==0xA5);
    int32_t key=-1;float point[4]={0,0,.5f,1};
    assert(em_effects_live_001CCF70(point,&key)==0 && key==8);
    assert(em_effects_live_window(0x266930,0x90)==cable_sources);
    assert(em_effects_live_001CFBE0(0x8000,1,0x260000,block,0)==-1 && S.fault==0x260000);
    assert(em_effects_live_001CFA60(block,matrix,0,0)==-1);
    em_effects_live_detach();assert(S.other_tick==NULL && S.other_context==NULL);
    setup();memset(block,0xA5,sizeof block);S.hworkers.w_001CD370=NULL;
    assert(em_effects_live_001CFA60(block,matrix,0x3F000000,0x3E800000)==-1);
    assert(unknown_matrix_resolves==0);
    for(unsigned i=0;i<0x44;++i)assert(block[i]==0xA5);
    assert(rd32(block+0x44)==0x3F000000 && rd32(block+0x48)==0x3F800000);
    assert(rd32(block+0x4C)==0x3E800000 && rd32(block+0x50)==0x358637BD && rd32(block+0x54)==0);
    for(unsigned i=0x58;i<0x60;++i)assert(block[i]==0xA5);
}
int main(void) { spawn_and_fields();transforms();puts("PASS effects live aim API: spawn results, canonical fields, callback, generation, transform blocks, shared packets, missing-owner refusal");return 0; }
