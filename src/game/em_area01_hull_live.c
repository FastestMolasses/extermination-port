#include "game/em_area01_hull_live.h"
#include <string.h>

static uint32_t word(const uint8_t *p)
{ uint32_t v;memcpy(&v,p,sizeof v);return v; }

int em_area01_hull_chain(void *ctx,const EmActor *body,EmCollHullChain *out)
{
    EmArea01HullView *v=ctx;
    if(!v || !body || !out || !v->host.record || !v->host.resource_rest || !v->host.slot_bytes)return -1;
    uint8_t record[EM_ACTOR_RECORD_SIZE];
    if(v->host.record(v->host.ctx,body,record)<0)return -1;
    uint32_t address=word(record+0x58),size=0;
    if(!address || (address&3u))return -1;
    const uint8_t *chain=v->host.resource_rest(v->host.ctx,address,&size);
    /* The original reads the first record's vertex count even when the
     * chain count is zero. Keep that read available to the sole walker. */
    if(!chain || size<12)return -1;
    unsigned count=record[9];
    if(count>EM_OWNER_SERVICES_MAX_BONES)return -1;
    for(unsigned i=0;i<count;++i) {
        uint32_t slot=word(record+0x110+4*i);
        if(slot>UINT32_MAX-0x90u)return -1;
        const uint8_t *matrix=v->host.slot_bytes(v->host.ctx,(slot+0x90u)&~15u,64);
        if(!matrix)return -1;
        memcpy(v->matrices+16*i,matrix,64);
    }
    *out=(EmCollHullChain){chain,size,v->matrices,count};
    return 0;
}
