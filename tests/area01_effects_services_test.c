/* Reuse the existing owner fixture; its synthetic no-light/no-sound table
 * deliberately isolates the service/allocator/storage boundary. */
#define main previous_effects_contract_main
#include "aim_fire_effects_live_test.c"
#undef main
#include "game/em_area01_effects_services.h"

static EmArea01ActorView av;
static uint8_t operands[32];
static int projection(void *ctx, EmActor *a, EmArea01ActorSpan spans[EM_AREA01_ACTOR_SHARED_MAX])
{ (void)ctx; return em_area01_effects_services_project(a, spans); }
static uint8_t *operand(void *ctx, uint32_t at, uint32_t n, int write)
{
    (void)ctx;
    return !write && at >= 0x1E00000 && n <= sizeof operands && at-0x1E00000 <= sizeof operands-n ?
        operands+(at-0x1E00000) : NULL;
}
static void reset_effects(uint32_t callback)
{
    setup();
    write32(S.etables.bytes+0xC,callback);
    em_area01_actor_view_reset(&av,&pool,projection,NULL,NULL);
    av.written=em_area01_effects_services_written;
}
static uint32_t spawn(uint32_t function)
{
    const uint32_t vectors[8]={0x3F800000,0x40000000,0x40400000,0x40800000,
                              0x3E800000,0x3F000000,0xBF800000,0x3F800000};
    memcpy(operands,vectors,sizeof vectors);
    EmArea01Call c={.function=function,.na=function==0x1EFD90?3:2,.nf=function==0x1EF9D0?1:0,
                   .a={0x80000000,0x1E00000,0x1E00010},.f={0x3F800000}};
    EmArea01RuntimeHost host={.bytes=operand};
    EmArea01EffectsSpawn s;
    assert(em_area01_effects_services_prepare(&host,&c,&s)==0);
    memset(operands,0,sizeof operands); /* no source pointer survives prepare */
    assert(em_area01_effects_services_invoke(&s,&c)==0);
    return (uint32_t)c.v0;
}
static void projections(void)
{
    for(unsigned kind=0;kind<3;++kind) {
        reset_effects(kind==0?EM_EFFECTS_LIVE_DRIVER:kind==1?EM_EFFECTS_LIVE_HEAD_SPRITE:0x21AAC0);
        uint32_t node=spawn(0x1EFD90);
        EmActor *a=&pool.records[0];
        assert(node==EM_ACTOR_POOL_BASE);
        memset(a->scratch,0xA5,sizeof a->scratch);
        EmArea01ActorSpan spans[8]; int n=projection(NULL,a,spans);
        assert(n>0 && n<=8);
        uint8_t header[0x14], pos[16], work[16], sound[20];
        assert(em_area01_actor_view_snapshot(&av,node,sizeof header,header)==0);
        assert(em_area01_actor_view_snapshot(&av,node+0xB0,16,pos)==0);
        assert(em_area01_actor_view_snapshot(&av,node+0x1F0,16,work)==0);
        assert(em_area01_actor_view_snapshot(&av,node+0x2DC,20,sound)==0);
        assert(!av.active && !av.record[0].touched && !memcmp(pos,a->pos,16));
        assert(em_area01_actor_view_begin(&av)==0);
        assert(em_area01_actor_view_touch(&av,node)==0);
        uint8_t *p=em_area01_actor_view_bytes(&av,node+0x1F0,4,1);assert(p);write32(p,0x12345678);
        p=em_area01_actor_view_bytes(&av,node+0x244,4,1);assert(p);write32(p,0x3E800000);
        p=em_area01_actor_view_bytes(&av,node+0x200,4,1);assert(p);write32(p,0xABCDEF12);
        p=em_area01_actor_view_bytes(&av,node+0xD0,64,1);assert(p);memset(p,0x3C,64);
        p=em_area01_actor_view_bytes(&av,node+0x24,4,1);assert(p);write32(p,0x8102B0);
        assert(em_area01_actor_view_bytes(&av,node+0x24,4,0)==p);
        assert(em_area01_actor_view_commit(&av)==0);
        assert(rd32(em_effects_live_node_field(node+0x24,4,0))==0x8102B0);
        assert(rd32(em_effects_live_node_field(node+0x1F0,4,0))==0x12345678);
        assert(rd32(em_effects_live_node_field(node+0x244,4,0))==0x3E800000);
        assert(rd32(a->scratch+0x10)==0xABCDEF12);
        if(kind<2) assert(rd32(a->scratch)==0xA5A5A5A5 && rd32(a->scratch+0x54)==0xA5A5A5A5);
        else assert(rd32(a->scratch)==0x12345678);
        assert(em_area01_actor_view_begin(&av)==0 && em_area01_actor_view_touch(&av,node)==0);
        assert(em_area01_actor_view_commit(&av)==0);
        assert(em_actor_pool_free_001AFC10(&pool,&scene,a)==0);
        assert(!em_effects_live_node_identity(a,NULL));
        assert(em_actor_pool_alloc_001AFA90(&pool,&scene,4)==a && !em_effects_live_node_identity(a,NULL));
        assert(projection(NULL,a,spans)==0);
    }
}
static void refusals(void)
{
    for(unsigned test=0;test<9;++test) {
        reset_effects(EM_EFFECTS_LIVE_DRIVER);uint32_t node=spawn(0x1EFD90);
        assert(em_area01_actor_view_begin(&av)==0 && em_area01_actor_view_touch(&av,node)==0);
        if(test==0) assert(!em_area01_actor_view_bytes(&av,node+0x24,4,0));
        if(test==1) assert(!em_area01_actor_view_bytes(&av,node+0x25,1,1));
        if(test==2) assert(!em_area01_actor_view_bytes(&av,node+0x20,8,1));
        if(test==3) assert(!em_area01_actor_view_bytes(&av,node,EM_ACTOR_RECORD_SIZE,0));
        if(test==4) { av.record[0].image[0x24]=1;assert(em_area01_actor_view_commit(&av)<0); }
        if(test==5) { S.slot[0].e.work.seed=42;assert(em_area01_actor_view_commit(&av)<0); }
        if(test==6) { pool.records[0].scratch[0x10]=42;assert(em_area01_actor_view_commit(&av)<0); }
        if(test==7) { assert(em_actor_pool_free_001AFC10(&pool,&scene,&pool.records[0])==0);
                     assert(em_actor_pool_alloc_001AFA90(&pool,&scene,4)); assert(em_area01_actor_view_commit(&av)<0); }
        if(test==8) { assert(em_area01_actor_view_commit(&av)==0);
                     uint32_t out;assert(em_area01_actor_view_snapshot(&av,node+0x24,4,&out)<0);
                     assert(!em_effects_live_node_field(node+0x24,4,0)); continue; }
        assert(av.fault && !S.slot[0].parent24_valid);
    }
    reset_effects(0x21AAC0);EmArea01RuntimeHost h={.bytes=operand};
    EmArea01Call c={.function=0x1EFD90,.na=3,.a={0x80000000,0x1E00001,0x1E00010}};
    assert(em_area01_effects_services_call(&h,&c)<0 && !bound);
    c.a[1]=0x1E00000;c.a[2]=0x1E00020;assert(em_area01_effects_services_call(&h,&c)<0 && !bound);
    c.function=0x1EF9D0;c.na=2;c.nf=1;c.a[1]=0;c.f[0]=0x3F800000;
    assert(em_area01_effects_services_call(&h,&c)==0 && c.v0==EM_ACTOR_POOL_BASE);
    reset_effects(0);assert(spawn(0x1EFD20)==0);
    reset_effects(EM_EFFECTS_LIVE_DRIVER);uint32_t node=spawn(0x1EFD90), word;
    /* Missing completion hook makes this projection invalid. Instrumentation
     * must report failure without latching a gameplay fault or touching it. */
    av.written=NULL;
    assert(em_area01_actor_view_snapshot(&av,node,4,&word)<0);
    assert(!av.fault && !av.fault_address && !av.active && !av.record[0].touched);
    av.written=em_area01_effects_services_written;
    assert(em_area01_actor_view_snapshot(&av,node,4,&word)==0);
}
int main(int argc,char **argv)
{
    if(argc==3) {
        unsigned function=(unsigned)strtoul(argv[1],NULL,16), callback=(unsigned)strtoul(argv[2],NULL,16);
        reset_effects(callback);uint32_t node=spawn(function);
        uint8_t image[EM_ACTOR_RECORD_SIZE];
        em_actor_pool_record_image(&pool,&pool.records[0],image);
        if(node) memcpy(image+0x38,&S.slot[0].e.live38,4);
        printf("%08x ",node);
        for(unsigned i=0;i<sizeof image;++i)printf("%02x",image[i]);
        puts("");return 0;
    }
    projections();refusals();spawn_and_fields();transforms();
    puts("PASS AREA01 effects services: prepared operands/results, canonical projections, write-first +24, scratch ownership, lifetime and snapshots");
    return 0;
}
