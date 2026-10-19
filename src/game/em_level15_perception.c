/* 00184D20 perception predicate from the matching decomp C. UNWIRED.
 * Callers provide original memory and math workers; every access is borrowed. */
#include "em_level15_perception_internal.h"
#define B(a) l15p_u8(o,(a))
#define R(a) l15p_u32(o,(a))
#define F(a) l15p_bits(a)
#define C(fn,...) do { if (l15p_c_##fn(o, __VA_ARGS__)) return 0; } while (0)

static int32_t e00184D20(L15P *o, uint32_t actor, uint32_t sensor, int32_t event)
{
    uint32_t type=B(sensor+2)&31,sub,shape,dx,dy,dz,angle=0,dist,q,v,ax,az,qx,qz;
    if (type==4) {
        switch (event) {
        case 0x20: if (B(sensor+3)!=0x36) return 0; break;
        case 0x27: if (B(sensor+3)!=0x35) return 0; break;
        case 0x25: if (B(sensor+3)!=0x21) return 0; break;
        case 0x26: if (B(sensor+3)!=0x49) return 0; break;
        case 0x1B: case 0x1C: case 0x1D:
            sub=B(sensor+3);
            if (sub!=0x14 && sub!=0x22 && sub!=0x23 && sub!=0x24 && sub!=0x25 && sub!=0x26 && sub!=0x2C) return 0;
            break;
        default: return 0;
        }
    } else {
        if (type!=6) return 0;
        switch (event) {
        case 0x23: sub=B(sensor+3); if (sub!=0x2F && sub!=0x12) return 0; break;
        case 0x24: sub=B(sensor+3); if (sub!=0x13 && sub!=0x12 && sub!=0x2F) return 0; break;
        case 0x1B: case 0x1C: case 0x1D:
            sub=B(sensor+3);
            if (sub!=0x14 && sub!=0x22 && sub!=0x23 && sub!=0x24 && sub!=0x25 && sub!=0x26 && sub!=0x2C) return 0;
            break;
        default: return 0;
        }
    }
    shape=B(sensor+8);
    switch (shape) {
    default:
        dx=L15P_SUB(R(actor+0xA0),R(sensor+0xB0)); dy=L15P_SUB(R(actor+0xA8),R(sensor+0xB8));
        C(0011E748,L15P_ADD(L15P_MUL(dx,dx),L15P_MUL(dy,dy)),&dist);
        if (!L15P_LE(dist,R(R(sensor+0x30)))) return 0;
        dz=L15P_SUB(R(actor+0xA4),R(sensor+0xB4)); C(0011E748,L15P_MUL(dz,dz),&dist);
        if (!L15P_LE(dist,R(R(sensor+0x30)+4))) return 0;
        C(001B1470,L15P_SUB(L15P_ADD(F(3.1415927410125732f),R(actor+0xC4)),R(sensor+0xC4)),&angle);
        break;
    case 1:
        q=R(sensor+0x30); ax=R(actor+0xA0); az=R(actor+0xA8); qx=R(q); qz=R(q+8);
        dx=L15P_SUB(ax,qx); dy=L15P_SUB(az,qz);
        C(0011E748,L15P_ADD(L15P_MUL(dx,dx),L15P_MUL(dy,dy)),&dist);
        q=R(sensor+0x30); if (!L15P_LE(dist,R(q+12))) return 0;
        v=R(q+4); dz=L15P_SUB(R(actor+0xA4),v); C(0011E748,L15P_MUL(dz,dz),&dist);
        q=R(sensor+0x30); if (!L15P_LE(dist,R(q+16))) return 0;
        /* The retained class is 4 or 6 after the entry gate. The C's class-10
         * branch is impossible; the test proves and accounts for that block. */
        C(001B1470,L15P_SUB(L15P_ADD(F(3.1415927410125732f),R(actor+0xC4)),R(q+20)),&angle);
        break;
    case 2:
        q=R(sensor+0x30); ax=R(actor+0xA0); az=R(actor+0xA8); qx=R(q); qz=R(q+8);
        dx=L15P_SUB(qx,ax); dy=L15P_SUB(qz,az);
        C(0011E748,L15P_ADD(L15P_MUL(dx,dx),L15P_MUL(dy,dy)),&dist);
        q=R(sensor+0x30); if (!L15P_LE(dist,R(q+12))) return 0;
        dz=L15P_SUB(R(actor+0xA4),R(sensor+0xB4)); C(0011E748,L15P_MUL(dz,dz),&dist);
        q=R(sensor+0x30); if (!L15P_LE(dist,R(q+16))) return 0;
        angle=0; break;
    case 3: case 4:
        dx=L15P_SUB(R(sensor+0xB0),R(actor+0xA0)); dy=L15P_SUB(R(sensor+0xB8),R(actor+0xA8));
        C(0011E748,L15P_ADD(L15P_MUL(dx,dx),L15P_MUL(dy,dy)),&dist);
        q=R(sensor+0x30); if (!L15P_LE(dist,R(q))) return 0;
        dz=L15P_SUB(R(actor+0xA4),R(sensor+0xB4));
        if (L15P_LE(0,dz)) { if (!L15P_LE(dz,R(q+4))) return 0; }
        else { C(0011DF78,dz,&v); if (!L15P_LE(v,L15P_ADD(F(17.0f),R(R(sensor+0x30)+4)))) return 0; }
        if (B(sensor+8)==4) { C(001B1470,L15P_SUB(R(actor+0xC4),R(sensor+0xC4)),&angle); }
        else if (L15P_LE(dist,F(7.0f))) angle=0;
        else { C(0011E620,dx,dy,&v); C(001B1470,L15P_SUB(R(actor+0xC4),v),&angle); }
        C(0011DF78,angle,&v); return L15P_LE(v,F(1.5707963705062866f));
    case 5:
        dx=L15P_SUB(R(sensor+0xB0),R(actor+0xA0)); dy=L15P_SUB(R(sensor+0xB8),R(actor+0xA8));
        if (!L15P_LE(L15P_ADD(L15P_MUL(dx,dx),L15P_MUL(dy,dy)),F(196.0f))) return 0;
        C(0011DF78,L15P_SUB(R(sensor+0xB4),R(actor+0xA4)),&v);
        if (L15P_LT(F(4.0f),v)) return 0;
        return 1;
    }
    C(0011DF78,angle,&v); return L15P_LE(v,F(0.7853981852531433f));
}

int em_level15_perception_00184D20(const EmLevel15PerceptionHooks *h, uint32_t actor,
    uint32_t sensor, int32_t event, int32_t *result, EmLevel15PerceptionFault *fault)
{
    L15P o;
    if (!result || l15p_begin(&o,h,fault)) return -1;
    int32_t value=e00184D20(&o,actor,sensor,event);
    if (l15p_end(&o)) return -1;
    *result=value;
    return 0;
}
