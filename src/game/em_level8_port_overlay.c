/* AREA04 overlay functions of the eighth level (see em_level8_port.h,
 * docs/LEVEL8_PORT.md): runtime 0x823920, 0x8239A0, 0x823A90 and 0x8245F0
 * (OVERLAY/AREA04.BIN, id 5; the decomp's splat/link names are 0x40 lower:
 * func_overlay_AREA04_008238E0, _00823960, _00823A50, _008245B0). The
 * decomp's C of all four is byte-identical (lane A04C); the translation
 * follows it and the instructions' order of loads and stores (the rules of
 * em_level8_port_lift.c).
 */
#include "em_level8_port_internal.h"

/* ------------------------------------------------------------------------
 * 0x823920 (placement behaviour, group spawned at [3]'s first stage): in
 * state 1, when the frame counter 0x70003B68 is a multiple of 40 (the
 * signed remainder, as the EE's div leaves it in HI), 001EFEB0(0, self
 * +0xD0). States 0, 2, 3 and others: nothing.
 * ---------------------------------------------------------------------- */
int em_level8_port_00823920(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    if (l8_u8(&o, self + 4) == 1) {
        int32_t frame = (int32_t)l8_u32(&o, S_70003B68);
        if (frame % 40 == 0) l8_c_001EFEB0(&o, 0, self + 0xD0);
    }
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x823A90: the same with a period of 50 and 001EFD20(0x8000001B, self
 * +0x100).
 * ---------------------------------------------------------------------- */
int em_level8_port_00823A90(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    if (l8_u8(&o, self + 4) == 1) {
        int32_t frame = (int32_t)l8_u32(&o, S_70003B68);
        if (frame % 50 == 0) l8_c_001EFD20(&o, (int32_t)0x8000001Bu, self + 0x100);
    }
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8239A0 (the group's sparks, 16 live in a04b_03): w = self +0x1F0.
 * State 0: w[0] = 0 (stored before the call), w[1] = (float)00122BB8() /
 * 2^31, +4 = 1, and on into state 1. State 1: 001D04B0(self +0xD0, 1,
 * 0x8274A0, w[0], w[1]); w[0] += 0.01 (stored); above 2.0 (not <= 2.0)
 * +4 = 3. States 2, 3: 001AFC10(self). Others: nothing.
 * ---------------------------------------------------------------------- */
int em_level8_port_008239A0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    uint32_t w = self + 0x1F0;
    uint32_t state = l8_u8(&o, self + 4);
    if (state == 3 || state == 2) {
        l8_c_001AFC10(&o, self);
        return l8_end(&o);
    }
    if (state == 0) {
        l8_w32(&o, w, 0);
        int32_t r = 0;
        if (l8_c_00122BB8(&o, &r)) return -1;
        l8_w32(&o, w + 4, L8_DIV(L8_CVT(r), F_2P31));
        l8_w8(&o, self + 4, 1);
    } else if (state != 1) {
        return l8_end(&o);
    }
    uint32_t t = l8_u32(&o, w);
    uint32_t phase = l8_u32(&o, w + 4);
    if (l8_c_001D04B0(&o, self + 0xD0, 1, 0x008274A0u, fl(t), fl(phase))) return -1;
    uint32_t next = L8_ADD(l8_u32(&o, w), F_0_01);
    int within = L8_LE(next, F_2);
    l8_w32(&o, w, next);
    if (!within) l8_w8(&o, self + 4, 3);
    return l8_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8245F0 (the NPC [2]'s second sub-state; called from 0x824320). By +5:
 * 0 when +0x0B bit 2 (the Use) is set: script 0x8280D0 on self +0x1F0
 * (001BA1A0) and +5 = 1; 1 when 001BA1F0(self) is nonzero: +5 = 2, +0x0B =
 * 0, +5 = 0 (both stores of +5 are made) and 001C67E0(self, 0, 20, 0).
 * Then, for every +5: 001BA580(self, +0x0D) and 001C64F0(self, 1.0).
 * ---------------------------------------------------------------------- */
int em_level8_port_008245F0(const EmLevel8PortHooks *h, uint32_t self, EmLevel8PortFault *fault)
{
    L8 o;
    if (l8_begin(&o, h, fault)) return -1;
    uint32_t sub = l8_u8(&o, self + 5);
    if (sub == 1) {
        int32_t r = 0;
        if (l8_c_001BA1F0(&o, self, &r)) return -1;
        if (r) {
            l8_w8(&o, self + 5, 2);
            l8_w8(&o, self + 0x0B, 0);
            l8_w8(&o, self + 5, 0);
            if (l8_c_001C67E0(&o, self, 0, fl(F_20), fl(F_ZERO))) return -1;
        }
    } else if (sub == 0) {
        if (l8_u8(&o, self + 0x0B) & 4u) {
            if (l8_c_001BA1A0(&o, self + 0x1F0, 0x008280D0u)) return -1;
            l8_w8(&o, self + 5, 1);
        }
    }
    if (l8_c_001BA580(&o, self, (int32_t)l8_u8(&o, self + 0x0D))) return -1;
    int32_t frame = 0;
    l8_c_001C64F0(&o, self, fl(F_ONE), &frame);
    return l8_end(&o);
}
