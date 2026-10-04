/* Test-only canonical RAM views; production binds actual loader/actor spans. */
#include "game/em_area01_interaction_live.h"
#include <string.h>
static uint8_t *ram,*spr;
static EmArea01Interaction interaction;
static EmInteractionMath math;
static int hit;
static uint32_t flags,kind,owner,queries,claims;
static uint32_t query_words[EM_INTERACTION_CAPACITY][12];
static uint8_t *memory(void *ctx,uint32_t a,uint32_t n,int write)
{
    (void)ctx;(void)write;
    if(a>=0x70000000u && a-0x70000000u<0x4000 && n<=0x4000-(a-0x70000000u))return spr+a-0x70000000u;
    return a<0x2000000u && n<=0x2000000u-a ? ram+a : NULL;
}
static int worker(void *ctx,EmArea01Call *c)
{
    if(c->function!=0x19A910 || c->na!=3)return -1;
    if(queries>=EM_INTERACTION_CAPACITY)return -1;
    uint32_t *row=query_words[queries++];
    row[0]=(uint32_t)c->a[0];row[1]=(uint32_t)c->a[1];row[2]=(uint32_t)c->a[2];
    memcpy(row+3,memory(ctx,(uint32_t)c->a[0],16,0),16);
    memcpy(row+7,memory(ctx,(uint32_t)c->a[1],16,0),16);
    memcpy(row+11,spr+0x3B98,4);
    uint32_t record=0x950000;memcpy(spr+0x31D0,&record,4);
    memcpy(ram+record+0x1A,&flags,2);memcpy(spr+0x31D8,&kind,4);memcpy(spr+0x31D4,&owner,4);
    c->v0=hit;return 0;
}
static int claim(void *ctx,uint32_t node)
{
    (void)ctx;if(ram[node+11]!=4 || spr[0x3B8D])return -1;
    ++claims;spr[0x3B8D]=3;return 1;
}
int ai_seed(uint8_t *memory_,uint8_t *scratch,int h,uint32_t f,uint32_t k,uint32_t o)
{
    ram=memory_;spr=scratch;hit=h;flags=f;kind=k;owner=o;queries=claims=0;
    memset(query_words,0,sizeof query_words);memcpy(&math,ram+0x26C5D8,sizeof math);
    EmArea01RuntimeHost host={NULL,memory,worker};
    return em_area01_interaction_bind(&interaction,&host,&math,claim,NULL);
}
int ai_call(uint32_t fn,uint32_t player,uint32_t node,uint32_t *out)
{
    EmArea01Call c={.function=fn,.a={player,node},.na=fn==0x183EF0?2:1};
    int rc=em_area01_interaction_call(&interaction,&c);*out=(uint32_t)c.v0;return rc;
}
uint32_t ai_fault(void){return interaction.fault_address;}
uint32_t ai_queries(void){return queries;}
uint32_t ai_claims(void){return claims;}
const uint32_t *ai_query(unsigned i){return i<queries ? query_words[i] : NULL;}
