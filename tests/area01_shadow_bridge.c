#define EM_A01_TEST_SHADOW
#include "area01_model_draw_bridge.c"
#define S Shadow
#define load shadow_load
#define w_box shadow_box
#include "game/em_shadow_live.c"
#undef w_box
#undef load
#undef S

int em_camera_live_bound(void){return 1;}
uint8_t *em_camera_live_bytes(uint32_t a,uint32_t n){return em_rcl_bytes_mut(a,n);}
EmSceneState *em_scene_state(void){return &scene;}
int as_seed(uint8_t *memory,uint8_t *scratch,uint32_t node)
{
    if(ad_seed(memory,scratch,node)<0)return -1;
    Shadow.pass_count=0;Shadow.fault=0;
    if(em_shadow_live_select_area(ram[0x810700],ram[0x810701])<0)return -1;
    scene.d810700=ram[0x810700];scene.d810701=ram[0x810701];
    memcpy(Shadow.state.d817FF0,ram+0x817FF0,16);
    Shadow.workers=(EmShadowOriginalWorkers){NULL,w_bounds,w_alpha_clear,shadow_box,w_silhouette,
                                             w_receiver_begin,w_receiver,w_receiver_end};
    Shadow.bound=1;
    return 0;
}
int as_call(uint32_t node){return em_area01_model_draw_call(&draw_view,0x001DA6A0u,node);}
static int shadow_worker(void *ctx,uint32_t fn,uint32_t node,uint32_t arg)
{
    (void)ctx;(void)arg;
    return fn==0x001DA6A0u ? em_area01_model_draw_call(&draw_view,fn,node) : -1;
}
int as_face(uint32_t node)
{
    model.source.worker=shadow_worker;
    uint32_t out;return em_area01_model_call(&model,0x001BA580u,node,ram[node+0xD],0,0,0,0,&out);
}
void as_actor_snapshot(void){am_snapshot();}
const EmShadowOriginalPlan *as_plan(void){return &Shadow.aplan;}
const EmShadowOriginalState *as_state(void){return &Shadow.state;}
uint32_t as_commands(uint32_t *out,uint32_t capacity)
{
    Pass *p=Shadow.cur;
    if(!p || capacity<2*p->cmd_count)return 0;
    for(unsigned k=0;k<p->cmd_count;++k) {
        out[2*k]=p->cmd[k].op;out[2*k+1]=(uint32_t)p->cmd[k].index;
    }
    return p->cmd_count;
}
int as_select(unsigned area,unsigned sub){return em_shadow_live_select_area(area,sub);}
unsigned as_area(void){return Shadow.receiver_area;}
void as_clear_passes(void){Shadow.pass_count=0;}
