#include "game/em_area01_rcl_workers.h"
#include "game/em_packet_chain_original.h"
#include "game/em_render_context.h"
#include "game/em_render_context_live.h"
#include "game/em_shadow_decal_original.h"

static int fail(uint32_t *fault, uint32_t address)
{ if (fault && !*fault) *fault=address; return -1; }

int em_area01_rcl_workers_handles(uint32_t fn)
{
    return fn==0x001CB5F0u || fn==0x001CB950u || fn==0x001CB6B0u ||
           fn==0x001CB760u || fn==0x001D2E00u || fn==0x001D2DE0u;
}

typedef struct { EmPacketChain *chain; uint32_t packet; } Tex0;
static int allocate(void *ctx, uint32_t table, int32_t key, int32_t count, uint8_t **bytes)
{
    Tex0 *t=ctx;
    return em_packet_chain_001CB5F0(t->chain,table,key,count,&t->packet,bytes);
}

int em_area01_rcl_workers_call(EmArea01Call *c, uint32_t *fault)
{
    if (!c || !fault) return -1;
    if (*fault) return -1;
    if (!em_area01_rcl_workers_handles(c->function)) return 1;
    const uint32_t fn=c->function;
    unsigned na=fn==0x001D2E00u ? 1u : fn==0x001D2DE0u ? 2u : fn==0x001CB6B0u ? 4u : 3u;
    if (c->na!=na || c->nf) return fail(fault,fn);
    EmPacketChain *pc=em_rcl_packet_chain();
    if (!pc || em_rcl_fault()) return fail(fault,fn);
    uint32_t result=0;
    int rc;
    if (fn==0x001D2E00u || fn==0x001D2DE0u) {
        /* The original's slot index is signed. Only mapped context slots
         * are a supported address domain; refuse before its signed shift.
         * This is a call-local view of RCL bytes, not a second context. */
        int32_t slot=(int32_t)c->a[0];
        if (slot<0 || slot>=8) return fail(fault,fn);
        uint32_t address=pc->d275670+EM_RC_CTX_SLOTS+4u*(uint32_t)slot;
        uint8_t *p=em_rcl_bytes_mut(address,4);
        if (!p) return fail(fault,address);
        EmRenderContextView view={address,4,p};
        EmRenderContext context={.world={pc->d275670,&view,1}};
        rc=fn==0x001D2E00u ? em_render_context_001D2E00(&context,slot,&result)
            : em_render_context_001D2DE0(&context,slot,(uint32_t)c->a[1]);
        if (rc<0) return fail(fault,context.fault.address ? context.fault.address : fn);
    } else if (fn==0x001CB950u) {
        Tex0 t={.chain=pc};
        EmShadowDecalWorkers workers={.ctx=&t,.w_001CB5F0=allocate};
        EmShadowDecal decal={.workers=&workers};
        rc=em_shadow_decal_001CB950(&decal,(uint32_t)c->a[0],(int32_t)c->a[1],c->a[2]);
        result=t.packet;
        if (rc<0) return fail(fault,pc->fault_address ? pc->fault_address : decal.fault.address);
    } else if (fn==0x001CB5F0u) {
        rc=em_packet_chain_001CB5F0(pc,(uint32_t)c->a[0],(int32_t)c->a[1],
                                    (int32_t)c->a[2],&result,NULL);
    } else if (fn==0x001CB6B0u) {
        rc=em_packet_chain_001CB6B0(pc,(uint32_t)c->a[0],(int32_t)c->a[1],
                                    (int32_t)c->a[2],c->a[3]);
    } else {
        rc=em_packet_chain_001CB760(pc,(uint32_t)c->a[0],(int32_t)c->a[1],c->a[2]);
    }
    if (rc<0) return fail(fault,pc->fault_address ? pc->fault_address : fn);
    if (fn==0x001CB5F0u || fn==0x001CB950u || fn==0x001D2E00u)
        c->v0=(uint64_t)(int64_t)(int32_t)result;
    return 0;
}
