#include "game/em_area01_gun_aux.h"
int ag_call(const EmArea01RuntimeHost *host,EmArea01Call *call,uint32_t *fault,int sdk_leaves)
{
    EmSdkMathContext sdk={0};
    sdk.world.d26C5D0=(const int32_t *)(const void *)host->bytes(host->ctx,0x0026C5D0u,4,0);
    return em_area01_gun_aux_call(host,sdk_leaves?&sdk:0,call,fault);
}
