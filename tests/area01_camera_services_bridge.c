#include "game/em_area01_camera_services.h"
#include <string.h>
static uint32_t vectors[16];
static int deny;
static uint64_t result;
static uint8_t *bytes(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;
    if((write && deny) || !n || a<0x008105D0u || n>sizeof vectors ||
       a-0x008105D0u>sizeof vectors-n)return NULL;
    return (uint8_t *)vectors+a-0x008105D0u;
}
void *cvectors(void){return vectors;}
uint64_t cresult(void){return result;}
int ccall(uint32_t fn,uint32_t a,uint32_t b,uint32_t x,uint32_t y,int refuse)
{
    deny=refuse;const EmArea01RuntimeHost h={.bytes=bytes};
    EmArea01Call c={.function=fn,.a={a,b},.f={x,y},
                    .na=fn==0x0018C4B0u?1u:2u,.nf=fn==0x0018C4B0u?2u:1u};
    int rc=em_area01_camera_services_call(&h,&c);result=c.v0;return rc;
}
