#include "game/em_area01_camera_services.h"
#include "game/em_camera_follow_original.h"

int em_area01_camera_services_call(const EmArea01RuntimeHost *h,EmArea01Call *c)
{
    if(!c)return -1;
    if(c->function!=0x0018C4B0u && c->function!=0x0018C6A0u)return 1;
    if(!h || !h->bytes)return -1;
    int result,rc;
    if(c->function==0x0018C4B0u) {
        if(c->na!=1 || c->nf!=2)return -1;
        uint32_t a=(uint32_t)c->a[0];
        if(a&3)return -1;
        uint32_t *v=(uint32_t *)(void *)h->bytes(h->ctx,a,12,1);
        if(!v)return -1;
        rc=em_camera_follow_0018C4B0(v,c->f[0],c->f[1],&result);
    } else {
        if(c->na!=2 || c->nf!=1)return -1;
        uint32_t a=(uint32_t)c->a[0],b=(uint32_t)c->a[1];
        if((a|b)&3)return -1;
        const uint32_t *src=(const uint32_t *)(const void *)h->bytes(h->ctx,a,12,0);
        uint32_t *dst=(uint32_t *)(void *)h->bytes(h->ctx,b,12,1);
        if(!src || !dst)return -1;
        rc=em_camera_follow_0018C6A0(src,dst,c->f[0],&result);
    }
    if(rc<0)return -1;
    c->v0=(uint64_t)(int64_t)result;return 0;
}
