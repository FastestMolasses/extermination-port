/* The twelfth level: the AREA19 overlay functions the a19b census found
 * new (overlay id 16, runtime addresses; the overlay is linked 0x40 below
 * where it runs): the effect 0x824BE0, the group 0x82B5A0's behaviours
 * 0x824EF0 / 0x824F70 / 0x825030, the area init 0x8250C0, [6]'s second
 * stage 0x825420 and [7]'s second sequence 0x825930.
 * docs/LEVEL12_PORT.md section 2. Ground truth: the original
 * instructions (the test runs every entry against them). */
#include "em_level12_port_internal.h"

/* ------------------------------------------------------------------------
 * 0x824BE0 (self, sp): an effect. self +4: 2 / 3 -> 001AFC10(self). 0: by
 * self +0xD the phase blk +0 (blk = self +0x1F0) = 0.1 then 0.3 (0), 0.3
 * (1), untouched (others); blk +4 = 00122BB8(); +4 = 1; then as 1. 1: the
 * seed s = blk +4, h = 001CCF70(self +0x100); self +0xD 0 / 1: twice
 * 001CFA60(the packet at sp - 0x60, self +0xD0, blk +0, ((s >> 16) &
 * 0xFFFF) / 65535 + 0.0001), the seed stepping s * 37 + 11 between, each
 * followed by 001CFBE0(h, 0 / 1, the model 0x82B480 / 0x82B510 (+0xD 0) or
 * 1, 0x82B360 / 0x82B3F0 (+0xD 1), the packet, 0); blk +0 += 0.03, above
 * 1.5 -> +4 = 3.
 * ---------------------------------------------------------------------- */
static void l12_824BE0_pair(L12 *o, uint32_t self, uint32_t sp, int32_t h, uint32_t seed, int32_t f0, uint32_t m0,
                            uint32_t m1)
{
    uint32_t blk = self + 0x1F0, pkt = sp - 0x60u;
    uint32_t phase = l12_u32(o, blk);
    uint32_t r = L12_ADD(L12_DIV(L12_CVT_S_W((uint32_t)l12_sra(seed, 16) & 0xFFFFu), 0x477FFF00u), 0x38D1B717u);
    seed = seed * 37u + 11u;
    if (l12_c_001CFA60(o, pkt, self + 0xD0, phase, r)) return;
    if (l12_c_001CFBE0(o, h, f0, m0, pkt, 0)) return;
    phase = l12_u32(o, blk);
    r = L12_ADD(L12_DIV(L12_CVT_S_W((uint32_t)l12_sra(seed, 16) & 0xFFFFu), 0x477FFF00u), 0x38D1B717u);
    if (l12_c_001CFA60(o, pkt, self + 0xD0, phase, r)) return;
    if (l12_c_001CFBE0(o, h, 1, m1, pkt, 0)) return;
    {
        uint32_t t = L12_ADD(l12_u32(o, blk), 0x3CF5C28Fu);
        l12_w32(o, blk, t);
        if (!L12_LE(t, 0x3FC00000u)) l12_w8(o, self + 4, 3);
    }
}

void l12_00824BE0(L12 *o, uint32_t self, uint32_t sp)
{
    uint32_t blk = self + 0x1F0;
    uint32_t st = l12_u8(o, self + 4);
    if (st == 3 || st == 2) {
        l12_c_001AFC10(o, self);
        return;
    }
    if (st == 0) {
        uint32_t d = l12_u8(o, self + 0xD);
        if (d == 1) {
            l12_w32(o, blk, 0x3E99999Au);
        } else if (d == 0) {
            l12_w32(o, blk, 0x3DCCCCCDu);
            l12_w32(o, blk, 0x3E99999Au);
        }
        int32_t r;
        if (l12_c_00122BB8(o, &r)) return;
        l12_w32(o, blk + 4, (uint32_t)r);
        l12_w8(o, self + 4, 1);
    } else if (st != 1) {
        return;
    }
    {
        uint32_t seed = l12_u32(o, blk + 4);
        int32_t h;
        if (l12_c_001CCF70(o, self + 0x100, &h)) return;
        uint32_t d = l12_u8(o, self + 0xD);
        if (d == 1) l12_824BE0_pair(o, self, sp, h, seed, 1, 0x82B360u, 0x82B3F0u);
        else if (d == 0) l12_824BE0_pair(o, self, sp, h, seed, 0, 0x82B480u, 0x82B510u);
    }
}

int em_level12_port_00824BE0(const EmLevel12PortHooks *h, uint32_t self, uint32_t sp, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00824BE0(&o, self, sp);
    return l12_end(&o);
}

/* 0x824EF0 (self): in state 1 001EFEB0(0x8000004B, self +0xD0) and
 * 001EFEB0(4, self +0xD0). */
int em_level12_port_00824EF0(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    if (l12_u8(&o, self + 4) == 1u) {
        if (!l12_c_001EFEB0(&o, (int32_t)0x8000004Bu, self + 0xD0)) l12_c_001EFEB0(&o, 4, self + 0xD0);
    }
    return l12_end(&o);
}

/* 0x824F70 (self): in state 1 the matrix self +0xD0 copied to 0x700036A0
 * (00102958), its rows 0..2 scaled by -1 (00103230), 001EFEB0(5,
 * 0x700036A0). */
int em_level12_port_00824F70(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    if (l12_u8(&o, self + 4) == 1u) {
        if (l12_c_00102958(&o, 0x700036A0u, self + 0xD0)) return -1;
        for (uint32_t row = 0x700036A0u; row <= 0x700036C0u; row += 0x10u)
            if (l12_c_00103230(&o, row, row, 0xBF800000u)) return -1;
        l12_c_001EFEB0(&o, 5, 0x700036A0u);
    }
    return l12_end(&o);
}

/* 0x825030 (self): in state 1 the colour (0x30, 0x80, 0x30, 0x80) at
 * 0x700038B0 and 001F4E20(self +0x100, it, 5). */
int em_level12_port_00825030(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    if (l12_u8(&o, self + 4) == 1u) {
        l12_w32(&o, 0x700038B0u, 0x30);
        l12_w32(&o, 0x700038B4u, 0x80);
        l12_w32(&o, 0x700038B8u, 0x30);
        l12_w32(&o, 0x700038BCu, 0x80);
        l12_c_001F4E20(&o, self + 0x100, 0x700038B0u, 0x40A00000u);
    }
    return l12_end(&o);
}

/* 0x8250C0 (): the area's table words D_00275C2C = 3, D_00275C28 = 0x20,
 * D_00275C20 = 0x82F880, D_00275C24 = 0, D_00275C1C = 0x84D9C0. */
int em_level12_port_008250C0(const EmLevel12PortHooks *h, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_w32(&o, 0x275C2Cu, 3);
    l12_w32(&o, 0x275C28u, 0x20);
    l12_w32(&o, 0x275C20u, 0x82F880u);
    l12_w32(&o, 0x275C24u, 0);
    l12_w32(&o, 0x275C1Cu, 0x84D9C0u);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825420 (self, sp): [6]'s second stage. The area 0x82BD40 (4
 * quadwords) copied to sp - 0x40; self +0xB0 = 0x82BD80 (00102948); self
 * +0xC4 = -0.83775806. self +5: 0 -> 001B1EA0(0, 0x810350, the copy, 4)
 * == 1 with 190 <= the player's y (0x810354) <= 200: 001BA1A0(self
 * +0x1F0, 0x82BA00), +0x2E = 0, +5 = 1; 1 -> 001BA1F0(self) nonzero: +5 =
 * 2, +0x2E = 0xFFFF, D_008107F5 = 0xFF, 001C67E0(self, 1, 0, 0),
 * 001FAE70(0). Then with 0x70003B92 zero: 001C64F0(self, 1.0),
 * 001B17A0(self), 001C68C0(self) and the +0x4C method.
 * ---------------------------------------------------------------------- */
void l12_00825420(L12 *o, uint32_t self, uint32_t sp)
{
    uint32_t area = sp - 0x40u;
    uint8_t q[4][16];
    for (unsigned k = 0; k < 4; k++) l12_q(o, 0x82BD40u + 0x10u * k, q[k]);
    for (unsigned k = 0; k < 4; k++) l12_wq(o, area + 0x10u * k, q[k]);
    if (l12_c_00102948(o, self + 0xB0, 0x82BD80u)) return;
    l12_w32(o, self + 0xC4, 0xBF567750u);
    switch (l12_u8(o, self + 5)) {
    case 0: {
        int32_t r;
        if (l12_c_001B1EA0(o, 0, 0x810350u, area, 4, &r)) return;
        if (r != 1) break;
        uint32_t y = l12_u32(o, 0x810354u);
        if (L12_LT(y, 0x433E0000u)) break;
        if (!L12_LE(y, 0x43480000u)) break;
        if (l12_c_001BA1A0(o, self + 0x1F0, 0x82BA00u)) return;
        l12_w16(o, self + 0x2E, 0);
        l12_w8(o, self + 5, 1);
        break;
    }
    case 1: {
        int32_t r;
        if (l12_c_001BA1F0(o, self, &r)) return;
        if (r == 0) break;
        l12_w8(o, self + 5, 2);
        l12_w16(o, self + 0x2E, 0xFFFF);
        l12_w8(o, 0x8107F5u, 0xFF);
        if (l12_c_001C67E0(o, self, 1, 0, 0)) return;
        if (l12_c_001FAE70(o, 0)) return;
        break;
    }
    default:
        break;
    }
    if (l12_u8(o, 0x70003B92u) != 0) return;
    if (l12_c_001C64F0(o, self, F_ONE)) return;
    if (l12_c_001B17A0(o, self)) return;
    if (l12_c_001C68C0(o, self)) return;
    l12_method(o, self);
}

int em_level12_port_00825420(const EmLevel12PortHooks *h, uint32_t self, uint32_t sp, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00825420(&o, self, sp);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825930 (self): [7]'s second sequence. self +5 0 with bit 2 of self
 * +0xB: +5 = 1, 001BA1A0(self +0x1F0, 0x82BD90), 001B6F00(self, (0, 0,
 * 5.5, 1) at 0x700038A0, pi). +5 1: 001C64F0(self, 1.0); by D_008107F6:
 * 4 -> 001E8B40(1), 001FBD50(self, 0x8DF, 0, 1000), D_008107F6 += 1; 0xFF
 * -> D_00810854 |= 4; then 001BA1F0(self) nonzero -> +5 = 2.
 * ---------------------------------------------------------------------- */
void l12_00825930(L12 *o, uint32_t self)
{
    uint32_t st = l12_u8(o, self + 5);
    if (st == 0) {
        if (!(l12_u8(o, self + 0xB) & 4u)) return;
        l12_w8(o, self + 5, 1);
        if (l12_c_001BA1A0(o, self + 0x1F0, 0x82BD90u)) return;
        l12_w32(o, 0x700038A0u, 0);
        l12_w32(o, 0x700038A4u, 0);
        l12_w32(o, 0x700038A8u, 0x40B00000u);
        l12_w32(o, 0x700038ACu, F_ONE);
        l12_c_001B6F00(o, self, 0x700038A0u, 0x40490FDBu);
        return;
    }
    if (st != 1) return;
    if (l12_c_001C64F0(o, self, F_ONE)) return;
    switch (l12_u8(o, 0x8107F6u)) {
    case 0xFF:
        l12_w8(o, 0x810854u, l12_u8(o, 0x810854u) | 4u);
        break;
    case 4:
        l12_001E8B40(o, 1);
        if (l12_failed(o)) return;
        if (l12_c_001FBD50(o, self, 0x8DF, 0, 0x447A0000u)) return;
        l12_w8(o, 0x8107F6u, l12_u8(o, 0x8107F6u) + 1u);
        break;
    default:
        break;
    }
    {
        int32_t r;
        if (l12_c_001BA1F0(o, self, &r)) return;
        if (r != 0) l12_w8(o, self + 5, 2);
    }
}

int em_level12_port_00825930(const EmLevel12PortHooks *h, uint32_t self, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00825930(&o, self);
    return l12_end(&o);
}
