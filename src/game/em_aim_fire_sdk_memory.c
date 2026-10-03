#include "game/em_aim_fire_sdk_memory.h"
#include "game/em_owner_services_original.h"
#include "game/em_effect_original.h"
#include "game/em_coll_probe_original.h"
#include "game/em_sdk_vu0.h"
#include <string.h>

typedef struct {
    void *context;
    void *(*map)(void *,uint32_t,size_t,int);
} Memory;
#define TRY(x) do { if ((x)<0) return -1; } while (0)
static int read_bytes(Memory *m,uint32_t a,void *p,size_t n)
{
    const void *v=m->map(m->context,a,n,0);
    if (!v) return -1;
    memcpy(p,v,n);return 0;
}
static int write_bytes(Memory *m,uint32_t a,const void *p,size_t n)
{
    void *v=m->map(m->context,a,n,1);
    if (!v) return -1;
    memcpy(v,p,n);return 0;
}
int em_aim_fire_sdk_memory_call(void *context,
    void *(*map)(void *,uint32_t,size_t,int),EmAimFireTargetCall *f)
{
    if (!map || !f) return -1;
    Memory memory={context,map},*m=&memory;
    uint32_t a=(uint32_t)f->a[0],b=(uint32_t)f->a[1],c=(uint32_t)f->a[2];
    float in[16],out[16],v[4];
    switch (f->function) {
    case 0x102948: case 0x102958: {
        unsigned count=f->function==0x102948 ? 1 : 4;
        /* LQ/SQ discard the low address bits. All loads precede every store. */
        a&=~15u;b&=~15u;
        for (unsigned i=0;i<count;++i) TRY(read_bytes(m,b+16*i,in+4*i,16));
        for (unsigned i=0;i<count;++i) TRY(write_bytes(m,a+16*i,in+4*i,16));
        return 0;
    }
    case 0x1031E0:
        /* Three separate LW/SW pairs, including forward-overlap propagation. */
        for (unsigned i=0;i<3;++i) {
            uint32_t word;TRY(read_bytes(m,b+4*i,&word,4));TRY(write_bytes(m,a+4*i,&word,4));
        }
        return 0;
    case 0x1029C0:
        if (em_owner_services_identity_001029C0(out)!=0) return -1;
        a&=~15u;
        for (unsigned i=4;i--;) TRY(write_bytes(m,a+16*i,out+4*i,16));
        return 0;
    case 0x102918:
        a&=~15u;b&=~15u;c&=~15u;
        /* The VU load reads all four translation lanes, even though only
         * xyz participate in the arithmetic. Then row3 precedes rows0..2. */
        TRY(read_bytes(m,c,v,16));TRY(read_bytes(m,b+48,in+12,16));
        for (unsigned i=0;i<3;++i) TRY(read_bytes(m,b+16*i,in+4*i,16));
        if (em_owner_services_translate_00102918(out,in,v)!=0) return -1;
        for (unsigned i=0;i<4;++i) TRY(write_bytes(m,a+16*i,out+4*i,16));
        return 0;
    case 0x102B08: case 0x102BB0:
        a&=~15u;b&=~15u;
        for (unsigned i=0;i<4;++i) {
            /* The existing owner exposes four rows. Evaluate its first row
             * on a local matrix, then publish immediately before reading the
             * next original row. This retains partial-overlap behavior. The
             * other local rows are private and have no game-state effects. */
            memset(in,0,sizeof in);TRY(read_bytes(m,b+16*i,in,16));
            int r=f->function==0x102B08 ? em_owner_services_rotate_x_00102B08(out,in,f->f[0])
                                      : em_owner_services_rotate_y_00102BB0(out,in,f->f[0]);
            if (r!=0) return -1;
            TRY(write_bytes(m,a+16*i,out,16));
        }
        return 0;
    case 0x102C58: {
        /* 00102C58(dst, src, angles): 00102A60 (z), then 00102BB0 (y) and
         * 00102B08 (x) on dst (em_owner_services_euler_00102C58, the boxes'
         * owner of it). Each turn's output row depends only on its own
         * input row, so a whole-matrix pass equals the row passes when dst
         * is src or apart from it; a partial overlap is refused. The debris
         * node 001F2BA0 calls it in place on its +0xD0. */
        a&=~15u;b&=~15u;c&=~15u;
        if (a!=b && a<b+64 && b<a+64) return -1;
        float angles[4];
        for (unsigned i=0;i<4;++i) TRY(read_bytes(m,b+16*i,in+4*i,16));
        TRY(read_bytes(m,c,angles,16));
        if (em_owner_services_euler_00102C58(out,in,angles)!=0) return -1;
        for (unsigned i=0;i<4;++i) TRY(write_bytes(m,a+16*i,out+4*i,16));
        return 0;
    }
    case 0x1026A0:
        a&=~15u;b&=~15u;c&=~15u;
        for (unsigned i=0;i<4;++i) TRY(read_bytes(m,b+16*i,in+4*i,16));
        TRY(read_bytes(m,c,v,16));
        em_effect_original_001026A0(out,in,v);
        return write_bytes(m,a,out,16);
    case 0x1026D0: {
        /* 001026D0(dst, a, b), em_sdk_vu0_001026D0's order: the four rows
         * of a first, then per row of b its load, the product row and its
         * store before the next row is read (dst may be a or b). */
        a&=~15u;b&=~15u;c&=~15u;
        uint32_t rows[16];
        TRY(read_bytes(m,b,rows,64));
        for (unsigned i=0;i<4;++i) {
            uint32_t one[16],o[16];
            memset(one,0,sizeof one);
            TRY(read_bytes(m,c+16*i,one,16));
            /* Row i of the product is em_sdk_vu0_001026D0's row 0 of
             * (rows, one row of b). */
            if (em_sdk_vu0_001026D0(o,rows,one)!=EM_EE_FLOAT_OK) return -1;
            TRY(write_bytes(m,a+16*i,o,16));
        }
        return 0;
    }
    case 0x102760:
        a&=~15u;b&=~15u;
        TRY(read_bytes(m,b,in,16));em_effect_original_00102760(out,in);
        return write_bytes(m,a,out,16);
    case 0x102718:
        a&=~15u;b&=~15u;c&=~15u;
        TRY(read_bytes(m,b,in,16));TRY(read_bytes(m,c,v,16));
        em_effect_original_00102718(out,in,v);
        return write_bytes(m,a,out,16);
    case 0x102738:
        a&=~15u;b&=~15u;
        TRY(read_bytes(m,a,in,16));TRY(read_bytes(m,b,v,16));
        TRY(em_coll_probe_sdk_dot(out,in,v));f->f0=em_ee_bits(out[0]);return 0;
    case 0x1028D0: case 0x1028B8:
        a&=~15u;b&=~15u;c&=~15u;
        TRY(read_bytes(m,b,in,16));TRY(read_bytes(m,c,v,16));
        if (f->function==0x1028D0) TRY(em_coll_probe_sdk_sub(out,in,v));
        else TRY(em_coll_probe_sdk_add(out,in,v));
        return write_bytes(m,a,out,16);
    case 0x103230:
        a&=~15u;b&=~15u;
        TRY(read_bytes(m,b,in,16));
        TRY(em_coll_probe_sdk_scale(out,in,em_ee_float(f->f[0])));
        return write_bytes(m,a,out,16);
    case 0x102900: {
        a&=~15u;b&=~15u;
        uint32_t source[4],result[4];
        TRY(read_bytes(m,b,source,16));
        if (em_sdk_vu0_00102900(result,source,f->f[0])!=EM_EE_FLOAT_OK) return -1;
        return write_bytes(m,a,result,16);
    }
    default:return 1;
    }
}
