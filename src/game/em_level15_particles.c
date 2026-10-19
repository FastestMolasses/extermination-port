/* 0015A750 particle emitter from the decomp C, checked by the original oracle.
 * UNWIRED. Incoming count is explicit for the original's out-of-range kind. */
#include "em_level15_particles_internal.h"
#define R(a) l15s_u32(o,(a))
#define SH(a) l15s_s16(o,(a))
#define W(a,v) l15s_w32(o,(a),(v))
#define F(a) l15s_bits(a)
#define C(fn,...) do { if (l15s_c_##fn(o, __VA_ARGS__)) return; } while (0)

static void e0015A750(L15S *o, uint32_t actor, uint32_t sp, int32_t count)
{
    const uint32_t matrix=0x700036A0,work=sp-0x60;
    uint32_t rng,dt,phase,a,b,ratio,table,value;
    int32_t handle=0,kind;
    C(001029C0,matrix); C(00102918,matrix,matrix,actor+0xB0);
    W(matrix+0x34,L15S_ADD(R(matrix+0x34),F(2.0f)));
    C(001CCF70,actor+0xB0,&handle);
    dt=L15S_DIV(R(actor+0x20),F(60.0f)); rng=R(actor+0x1F4);
    kind=SH(actor+0x54);
    switch (kind) {
    case 0: W(0x248280,0x18); count=2; break;
    case 1: W(0x248280,0x20); count=3; break;
    case 2: W(0x248280,0x20); count=5; break;
    case 3: W(0x248280,0x18); count=3; break;
    case 4: W(0x248280,0x20); count=5; break;
    case 5: W(0x248280,0x18); count=3; break;
    case 6: W(0x248280,0x20); count=4; break;
    default: break;
    }
    for (int32_t i=1; i<count; i++) {
        phase=R(actor+0x1F0);
        a=L15S_ADD(phase,L15S_MUL(F(0.5f),L15S_ADD(L15S_DIV(L15S_CVT_S_W(rng>>16),F(65535.0f)),F(0.0001f))));
        rng=rng*37+11;
        if (!L15S_LE(a,F(2.0f))) a=L15S_SUB(a,F(1.0f));
        ratio=L15S_DIV(L15S_CVT_S_W((uint32_t)i+1),L15S_CVT_S_W(count));
        table=0x248120+(uint32_t)SH(actor+0x54)*20;
        W(0x248210,L15S_MUL(R(table),ratio));
        b=L15S_ADD(L15S_DIV(L15S_CVT_S_W(rng>>16),F(65535.0f)),F(0.0001f)); rng=rng*37+11;
        table=0x248120+(uint32_t)SH(actor+0x54)*20;
        W(0x248218,L15S_MUL(R(table+8),ratio));
        W(0x248210,L15S_SUB(R(0x248210),F(5.0f))); W(0x248218,L15S_SUB(R(0x248218),F(5.0f)));
        W(0x24822C,L15S_MUL(F(64.0f),dt)); W(0x24823C,0);
        C(001CFAE0,work,0,matrix,a,b,dt,F(0.1f)); C(001CFBE0,handle,1,0x248200,work,1);
    }
    value=L15S_ADD(R(actor+0x1F0),F(0.01f)); W(actor+0x1F0,value);
    if (!L15S_LE(value,F(2.0f))) W(actor+0x1F0,L15S_SUB(R(actor+0x1F0),F(1.0f)));
}

int em_level15_particles_0015A750(const EmLevel15ParticlesHooks *h, uint32_t actor,
    uint32_t entry_sp, int32_t incoming_count, EmLevel15ParticlesFault *fault)
{
    L15S o;
    if (l15s_begin(&o,h,fault)) return -1;
    e0015A750(&o,actor,entry_sp,incoming_count);
    return l15s_end(&o);
}
