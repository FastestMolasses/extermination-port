/* Test-only canonical render-memory provider. Production links the real RCL. */
#include "game/em_render_context_live.h"
#include "game/em_packet_chain_original.h"
#include <stddef.h>
static uint8_t *ram,*scratch;
static EmPacketChain chain;
static EmPacketChainRegion regions[2];
void em_aim_fire_render_fixture_bind(uint8_t *r,uint8_t *s)
{
    ram=r;scratch=s;regions[0]=(EmPacketChainRegion){0,0x2000000,r};
    regions[1]=(EmPacketChainRegion){0x70000000,0x4000,s};
    em_packet_chain_init(&chain,regions,2,EM_RCL_CONTEXT,0x814220);
}
static int contains(uint32_t a,uint32_t n,uint32_t base,uint32_t end)
{ return a>=base && (uint64_t)a+n<=end; }
const uint8_t *em_rcl_bytes(uint32_t a,uint32_t n)
{
    if (!ram) return NULL;
    if (contains(a,n,0x28F700,0x76B5C0) || contains(a,n,0x811CC0,0x817240) ||
        contains(a,n,0x275670,0x2756A0) || contains(a,n,0x250F30,0x253170)) return ram+a;
    if (contains(a,n,0x70003A40,0x70003B40)) return scratch+(a-0x70000000);
    return NULL;
}
uint8_t *em_rcl_bytes_mut(uint32_t a,uint32_t n)
{ return (uint8_t *)(uintptr_t)em_rcl_bytes(a,n); }
EmPacketChain *em_rcl_packet_chain(void)
{ return ram && !chain.fault ? &chain:NULL; }
