/* Test-only canonical memory provider; no captured state enters the game. */
#include "game/em_area01_matrix_service.h"
#include "game/em_owner_draw_live.c"
#include "game/em_load_veil_particles.h"
EmGameState g;
static uint8_t *ram,*spr,*stack;
static uint32_t denied;
static EmLoadVeilParticles veil;
uint32_t em_frame_counter(void){return 1;}
uint32_t em_rcl_fault(void){return 0;}
const uint8_t *em_rcl_bytes(uint32_t a,uint32_t n)
{
    if(a<0x2000000u && n<=0x2000000u-a)return ram+a;
    if(a>=0x70000000u && a<0x70004000u && n<=0x70004000u-a)return spr+a-0x70000000u;
    return NULL;
}
uint8_t *em_rcl_bytes_mut(uint32_t a,uint32_t n)
{return (uint8_t *)(uintptr_t)em_rcl_bytes(a,n);}
int em_rcl_001D1F80(int32_t a,int32_t b,int32_t c)
{return em_load_veil_particles_001D1F80(&veil,a,b,c);}
static uint8_t *memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;if(write || a==denied)return NULL;
    if(a>=0x7F000000u && a<0x7F100000u && n<=0x7F100000u-a)return stack+a-0x7F000000u;
    return em_rcl_bytes_mut(a,n);
}
void mx_seed(uint8_t *r,uint8_t *s,uint8_t *t)
{
    ram=r;spr=s;stack=t;denied=0;memset(&L,0,sizeof L);g.point_lights_loaded=1;
    memcpy(L.rig,ram+0x817BC0,sizeof L.rig);
    memcpy(L.spr.s3400,spr+0x3400,0xC0);memcpy(L.spr.s3AC0,spr+0x3AC0,64);
    memset(&veil,0,sizeof veil);
    veil.world.cursor=(uint32_t *)(void *)(ram+EM_RCL_CONTEXT+0x10);veil.world.cursor_count=4;
    veil.world.ctx_9C=(uint32_t *)(void *)(ram+EM_RCL_CONTEXT+0x9C);
    veil.world.d00275674=(uint32_t *)(void *)(ram+0x275674);
    veil.world.packet=ram;veil.world.packet_address=0;veil.world.packet_size=0x2000000u;
}
void mx_snapshot(void)
{
    memcpy(ram+0x817BC0,L.rig,sizeof L.rig);
    memcpy(spr+0x3400,L.spr.s3400,0xC0);memcpy(spr+0x3AC0,L.spr.s3AC0,64);
}
int mx_call(EmArea01Call *c,uint32_t *fault)
{
    EmArea01RuntimeHost host={NULL,memory,NULL};EmArea01MatrixService service;
    int rc=em_area01_matrix_service_prepare(&host,c,&service,fault);
    return rc ? rc : em_area01_matrix_service_invoke(&service,c);
}
int mx_model(uint32_t model)
{uint32_t w04;memcpy(&w04,ram+model+4,4);return em_owner_draw_live_001D3990_at(model,w04);}
void mx_deny(uint32_t address){denied=address;}
int mx_open(void){return L.open;}
uint32_t mx_open_start(void){return L.open_start;}
int mx_finish(uint32_t start,uint32_t *fault)
{
    EmArea01RuntimeHost host={NULL,memory,NULL};
    return em_area01_matrix_service_page_finish(&host,start,fault);
}
int mx_begin(uint32_t *start,uint32_t *fault)
{return em_area01_matrix_service_page_begin(start,fault);}
int mx_page(uint32_t address){return em_owner_draw_live_page_unit(address)!=NULL;}
int mx_triangles(uint32_t address,EmObjectUnitTriangle *out,uint32_t capacity)
{
    const EmObjectUnitPieces *p=em_owner_draw_live_page_unit(address);EmObjectUnitResult r={0};
    if(!p || em_object_unit_run(&p->unit,&r)<0){em_object_unit_result_free(&r);return -1;}
    int count=r.count>capacity ? -1 : (int)r.count;
    if(count>=0)memcpy(out,r.tri,r.count*sizeof *out);
    em_object_unit_result_free(&r);return count;
}
