#include "game/em_area01_matrix_service.h"
#include "game/em_owner_draw_live.h"
#include "game/em_render_context_live.h"
#include <string.h>

static int fail(uint32_t *fault,uint32_t address)
{ if(fault && !*fault)*fault=address;return -1; }
int em_area01_matrix_service_prepare(const EmArea01RuntimeHost *h,const EmArea01Call *c,
                                    EmArea01MatrixService *s,uint32_t *fault)
{
    if(!c || !fault || *fault)return -1;
    if(c->function!=0x001C7900u)return 1;
    if(!h || !h->bytes || !s || c->na!=4 || c->nf ||
       ((int32_t)c->a[3]!=0 && (int32_t)c->a[3]!=3))return fail(fault,c->function);
    uint32_t matrix=(uint32_t)c->a[0],token=(uint32_t)c->a[1];
    const uint8_t *p=h->bytes(h->ctx,matrix,64,0);
    if(!p)return fail(fault,matrix);
    memcpy(s->matrix,p,64);
    p=h->bytes(h->ctx,token,16,0);
    if(!p)return fail(fault,token);
    memcpy(s->token_bytes,p,16);s->token=token;
    s->vuaddr=(int32_t)c->a[2];s->channel=(int32_t)c->a[3];
    return 0;
}
int em_area01_matrix_service_invoke(const EmArea01MatrixService *s,EmArea01Call *c)
{
    if(!s || !c || c->function!=0x001C7900u)return -1;
    /* F4A10 does not consume the original's returned packet pointer. */
    return em_owner_draw_live_001C7900(s->matrix,s->token,s->token_bytes,s->vuaddr,s->channel);
}

int em_area01_matrix_service_page_begin(uint32_t *start,uint32_t *fault)
{
    if(!start || !fault || *fault)return -1;
    const uint8_t *p=em_rcl_bytes(EM_RCL_CONTEXT+0x1Cu,4);
    if(!p || em_rcl_fault())return fail(fault,EM_RCL_CONTEXT+0x1Cu);
    memcpy(start,p,4);return 0;
}
static const uint8_t *resource(void *ctx,uint32_t a,uint32_t n)
{
    const EmArea01RuntimeHost *h=ctx;
    const uint8_t *p=em_rcl_bytes(a,n);
    return p ? p : h && h->bytes ? h->bytes(h->ctx,a,n,0) : NULL;
}
int em_area01_matrix_service_page_finish(const EmArea01RuntimeHost *h,uint32_t start,uint32_t *fault)
{
    uint32_t end;
    if(em_area01_matrix_service_page_begin(&end,fault)<0)return -1;
    return em_owner_draw_live_page_register(start,end,resource,(void *)h)<0 ? fail(fault,start) : 0;
}
