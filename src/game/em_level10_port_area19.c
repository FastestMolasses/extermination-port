/* The tenth level's AREA19 overlay rows (OVERLAY/AREA19.BIN, id 16; runtime
 * addresses, the code is linked 0x40 lower): door [27] 0x823580, [6]
 * 0x8250F0 and its sub-state 0x825240, [43] 0x8255D0, [7] 0x8257C0 and its
 * sub-state 0x825AB0, [9] 0x825C70, [47] 0x825EE0, [18] 0x826100, [10]
 * 0x827790 and the probe 0x829370 (called by the group owner 0x827DD0).
 * See em_level10_port.h and docs/LEVEL10_PORT.md.
 *
 * The decomp's C of all of them is byte-identical (lane A13C,
 * docs/AREA19_OVERLAY.md); the translation follows the instructions' order
 * of loads and stores, which differs from the C text in places (noted at
 * each function). Roles in the comments are what the code does, not
 * placement labels. self = the pool node; talk = self + 0x1F0.
 */
#include "em_level10_port_internal.h"

/* overlay data (runtime addresses) */
#define A19_DOOR_LOCKS   0x00810841u /* the area lock bytes, indexed by D_00810700 */
#define A19_SCRIPT_27    0x0082AD50u
#define A19_AREA_6A      0x0082BD00u /* a 64-byte area record (001B1EA0) */
#define A19_SCRIPT_6A    0x0082B6E0u
#define A19_GROUP_6      0x0082A590u
#define A19_REC_7        0x0082C6D0u
#define A19_SCRIPT_7A    0x0082C410u
#define A19_SCRIPT_7B    0x0082C4D0u
#define A19_SCRIPT_7C    0x0082C690u
#define A19_SCRIPT_18    0x0082C6E0u
#define A19_AREA_10      0x0082E050u
#define A19_SCRIPT_10A   0x0082D990u
#define A19_SCRIPT_10B   0x0082DA10u
#define A19_SCRIPT_10C   0x0082DD10u
#define A19_TINT         0x0082F670u

static int32_t call_1B17A0(L10 *o, uint32_t self)
{
    int32_t r = 0;
    l10_c_001B17A0(o, self, &r);
    return r;
}

static int32_t call_1B0FD0(L10 *o, uint32_t self)
{
    int32_t r = 0;
    l10_c_001B0FD0(o, self, &r);
    return r;
}

static int32_t call_1BA1F0(L10 *o, uint32_t self)
{
    int32_t r = 0;
    l10_c_001BA1F0(o, self, &r);
    return r;
}

/* 001C64F0(self, 1.0), 001B17A0, 001C68C0 and the +0x4C method */
static void idle(L10 *o, uint32_t self)
{
    int32_t r = 0;
    l10_c_001C64F0(o, self, F_ONE, &r);
    (void)call_1B17A0(o, self);
    l10_c_001C68C0(o, self);
    l10_method(o, self);
}

/* 0x700038A0..AC = (x, y, z, w) */
static void put_vec(L10 *o, uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    l10_w32(o, S_700038A0, x);
    l10_w32(o, S_700038A4, y);
    l10_w32(o, S_700038A8, z);
    l10_w32(o, S_700038AC, w);
}

/* the 64-byte record at `from` copied to the frame at `to` (four quadword
 * loads, then four quadword stores) */
static void copy_area(L10 *o, uint32_t from, uint32_t to)
{
    uint8_t q[4][16];
    for (unsigned i = 0; i < 4; i++) l10_q(o, from + 16 * i, q[i]);
    for (unsigned i = 0; i < 4; i++) l10_wq(o, to + 16 * i, q[i]);
}

/* ------------------------------------------------------------------------
 * 0x823580: door [27]. +4: 0 001BBDA0(self), +0 = 1; 1 by +5 (the six
 * steps of the table 0x82F800, then 001BC300(self) for any +5, also one
 * above 5); 2 / 3 001AFC10. Step 0: the lock byte D_00810841[D_00810700]
 * (read after D_00810700 and the halfword +0x34) tested with bit
 * (+0x34 & 31): set -> 001BBE40(self, talk, 0) nonzero gives +5 = 3; clear
 * -> 001BBE40(self, talk, 1) nonzero gives +5 += 1. 1: 001BC0E0(self,
 * talk) nonzero -> 001BA1A0(talk, 0x82AD50), +5 += 1. 2: 001BC0E0 nonzero
 * -> 001C4760(0xA, 1) while D_00810CCD is 0, then +0xB = 0, +5 = 0. 3:
 * 001BC0E0 nonzero -> +5 += 1. 4: 001BC240(self, talk), +5 += 1. 5:
 * 001BC290(self, talk) nonzero -> +5 = 0.
 * ---------------------------------------------------------------------- */
int em_level10_port_00823580(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    uint32_t talk = self + 0x1F0;
    int32_t r = 0;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        l10_c_001BBDA0(&o, self);
        l10_w8(&o, self, 1);
        break;
    case 1:
        switch (l10_u8(&o, self + 5)) {
        case 0: {
            uint32_t area = l10_u8(&o, 0x00810700u);
            uint32_t bit = (uint32_t)l10_s16(&o, self + 0x34) & 31u;
            uint32_t locks = l10_u8(&o, A19_DOOR_LOCKS + area);
            if (locks & (1u << bit)) {
                l10_c_001BBE40(&o, self, talk, 0, &r);
                if (r) l10_w8(&o, self + 5, 3);
            } else {
                l10_c_001BBE40(&o, self, talk, 1, &r);
                if (r) {
                    uint32_t s = l10_u8(&o, self + 5);
                    l10_w8(&o, self + 5, s + 1);
                }
            }
            break;
        }
        case 1:
            l10_c_001BC0E0(&o, self, talk, &r);
            if (r) {
                l10_c_001BA1A0(&o, talk, A19_SCRIPT_27);
                uint32_t s = l10_u8(&o, self + 5);
                l10_w8(&o, self + 5, s + 1);
            }
            break;
        case 2:
            l10_c_001BC0E0(&o, self, talk, &r);
            if (r) {
                if (l10_u8(&o, 0x00810CCDu) == 0) {
                    int32_t ignored = 0;
                    l10_c_001C4760(&o, 0xA, 1, &ignored);
                }
                l10_w8(&o, self + 0xB, 0);
                l10_w8(&o, self + 5, 0);
            }
            break;
        case 3:
            l10_c_001BC0E0(&o, self, talk, &r);
            if (r) {
                uint32_t s = l10_u8(&o, self + 5);
                l10_w8(&o, self + 5, s + 1);
            }
            break;
        case 4: {
            l10_c_001BC240(&o, self, talk);
            uint32_t s = l10_u8(&o, self + 5);
            l10_w8(&o, self + 5, s + 1);
            break;
        }
        case 5:
            l10_c_001BC290(&o, self, talk, &r);
            if (r) l10_w8(&o, self + 5, 0);
            break;
        default:
            break;
        }
        l10_c_001BC300(&o, self);
        break;
    case 2:
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825240 (self; sp: the stack pointer at its entry; its frame is 0x70
 * and holds the area record at sp - 0x40). At entry, every call: the
 * 64-byte area 0x82BD00 copied to the frame. +5 0: 001B1EA0(0, the player
 * position 0x810350, the copy, 4) == 1 and the player's y (0x810354) not
 * below 210 -> 001BA1A0(talk, 0x82B6E0), +5 = 1, +0x28 = 0, D_008106C0 = 0
 * (below 210: nothing); != 1 -> the idle update (001C64F0(self, 1.0),
 * 001B17A0, 001C68C0, the +0x4C method). +5 1: when the signed +0x28 is
 * 0x424 and D_008106C0 is set, 001EFE00(0x8000002B, D_008106C0); +0x28 =
 * +0x28 + 1 (read again); 001BA1F0(self) nonzero -> 001AEE10(4, 0), +0x2E
 * = 0xFFFF, D_008107F5 |= 1 then |= 2 (each read and stored), 001C67E0(
 * self, 0, 0, 0), +5 = 0, 001E8B40(0), D_008106C0 = 001B6660(0x82A590),
 * 001FAE70(0).
 * ---------------------------------------------------------------------- */
void l10_00825240(L10 *o, uint32_t self, uint32_t sp)
{
    uint32_t area = sp - 0x40;
    uint32_t talk = self + 0x1F0;
    int32_t r = 0;
    copy_area(o, A19_AREA_6A, area);
    switch (l10_u8(o, self + 5)) {
    case 0:
        l10_c_001B1EA0(o, 0, D_00810350, area, 4, &r);
        if (r == 1) {
            uint32_t y = l10_u32(o, D_00810354);
            if (!L10_LT(y, 0x43520000u)) { /* 210.0 */
                l10_c_001BA1A0(o, talk, A19_SCRIPT_6A);
                l10_w8(o, self + 5, 1);
                l10_w16(o, self + 0x28, 0);
                l10_w32(o, 0x008106C0u, 0);
            }
        } else {
            idle(o, self);
        }
        break;
    case 1: {
        if (l10_s16(o, self + 0x28) == 0x424) {
            uint32_t other = l10_u32(o, 0x008106C0u);
            if (other) l10_c_001EFE00(o, (int32_t)0x8000002Bu, other, &r);
        }
        int32_t t = l10_s16(o, self + 0x28);
        l10_w16(o, self + 0x28, (uint32_t)(t + 1));
        if (call_1BA1F0(o, self)) {
            l10_c_001AEE10(o, 4, 0);
            l10_w16(o, self + 0x2E, 0xFFFF);
            uint32_t f = l10_u8(o, 0x008107F5u);
            l10_w8(o, 0x008107F5u, f | 1);
            f = l10_u8(o, 0x008107F5u);
            l10_w8(o, 0x008107F5u, f | 2);
            l10_c_001C67E0(o, self, 0, F_ZERO, F_ZERO);
            l10_w8(o, self + 5, 0);
            l10_c_001E8B40(o, 0);
            uint32_t handle = 0;
            l10_c_001B6660(o, A19_GROUP_6, &handle);
            l10_w32(o, 0x008106C0u, handle);
            l10_c_001FAE70(o, 0);
        }
        break;
    }
    default:
        break;
    }
}

int em_level10_port_00825240(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    l10_00825240(&o, self, sp);
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8250F0 (self; sp: its entry stack pointer, its frame 0x20, passed on
 * to 0x825240): [6]. +4 0: 001BA1C0(self, 0x1D) nonzero -> +4 = 3; else
 * 001B10B0(self, +0xD, 0x67), 001C63E0(self, 2) while D_00810775 is 0,
 * else 001C63E0(self, 0), 001CA6F0(self, 2), +4 = 1, +0 = 1. +4 1 by
 * D_008107F5: bit 0 clear -> 0x825240; else bit 2 clear -> 0x825420 (a
 * hook); else the idle update. +4 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_008250F0(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        l10_c_001BA1C0(&o, self, 0x1D, &r);
        if (r) {
            l10_w8(&o, self + 4, 3);
            break;
        }
        l10_c_001B10B0(&o, self, (int32_t)l10_u8(&o, self + 0xD), 0x67, &r);
        if (l10_u8(&o, 0x00810775u) == 0)
            l10_c_001C63E0(&o, self, 2);
        else
            l10_c_001C63E0(&o, self, 0);
        l10_c_001CA6F0(&o, self, 2);
        l10_w8(&o, self + 4, 1);
        l10_w8(&o, self, 1);
        break;
    case 1: {
        uint32_t f = l10_u8(&o, 0x008107F5u);
        if (!(f & 1))
            l10_00825240(&o, self, sp - 0x20);
        else if (!(f & 4))
            l10_c_00825420(&o, self);
        else
            idle(&o, self);
        break;
    }
    case 2:
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8255D0: [43]. +4 0: D_008107F5 bit 0 set -> +0xD = 0x25, then
 * 001B0FD0(self) zero -> 001C6380, +4 = 2, +0x2EC = 0; clear -> 001B0FD0
 * zero -> 001C6380, +4 = 1, 0x700038A0 = (0, 1, 0, 0.25), +0x2EC =
 * 001C5570(self, 0x700038A0, 0x1B, 0); when +0x2EC (read again) is set:
 * its +0xB0 += 0.5, and +0x2EC (read again) +0xB8 -= 0.5. +4 1: bit 0 set
 * -> 001CA6E0(self, 001C6120(D_0028A59C, 0x25)), 001C62C0, 001C6380, +4 =
 * 2, and when +0x2EC is set its +4 = 3 and +0x2EC = 0; then (and in +4 2)
 * the +0x4C method when 001B17A0 is nonzero. +4 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_008255D0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        if (l10_u8(&o, 0x008107F5u) & 1) {
            l10_w8(&o, self + 0xD, 0x25);
            if (call_1B0FD0(&o, self) == 0) {
                l10_c_001C6380(&o, self);
                l10_w8(&o, self + 4, 2);
                l10_w32(&o, self + 0x2EC, 0);
            }
        } else if (call_1B0FD0(&o, self) == 0) {
            l10_c_001C6380(&o, self);
            l10_w8(&o, self + 4, 1);
            put_vec(&o, F_ZERO, F_ONE, F_ZERO, F_QUARTER);
            uint32_t child = 0;
            l10_c_001C5570(&o, self, S_700038A0, 0x1B, 0, &child);
            l10_w32(&o, self + 0x2EC, child);
            child = l10_u32(&o, self + 0x2EC);
            if (child) {
                uint32_t x = l10_u32(&o, child + 0xB0);
                l10_w32(&o, child + 0xB0, L10_ADD(x, F_HALF));
                uint32_t again = l10_u32(&o, self + 0x2EC);
                uint32_t z = l10_u32(&o, again + 0xB8);
                l10_w32(&o, again + 0xB8, L10_SUB(z, F_HALF));
            }
        }
        break;
    case 1:
        if (l10_u8(&o, 0x008107F5u) & 1) {
            uint32_t table = l10_u32(&o, D_0028A59C);
            uint32_t model = 0;
            l10_c_001C6120(&o, table, 0x25, &model);
            l10_c_001CA6E0(&o, self, model);
            l10_c_001C62C0(&o, self);
            l10_c_001C6380(&o, self);
            l10_w8(&o, self + 4, 2);
            uint32_t child = l10_u32(&o, self + 0x2EC);
            if (child) {
                l10_w8(&o, child + 4, 3);
                l10_w32(&o, self + 0x2EC, 0);
            }
        }
        if (call_1B17A0(&o, self)) l10_method(&o, self);
        break;
    case 2:
        if (call_1B17A0(&o, self)) l10_method(&o, self);
        break;
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825AB0 (called by 0x8257C0 while D_008107F6 is 0): by +5. 0: when
 * D_00810702 is 0xA, 001BA1A0(talk, 0x82C410), +5 = 1. 1: 001BA1F0(self)
 * nonzero -> +5 = 2, +0x28 = 0, 001831F0(0), 001BA1A0(talk, 0x82C4D0), +7
 * = D_008102B5, D_008102B5 = 0; then 001831F0(2) either way. 2:
 * 001BA1F0(self) nonzero -> +5 = 3; +0x28 += 1 (signed, read again); from
 * 500 on: at exactly 500 D_008102B5 = +7; 001831F0(2). 3: D_008102B5 0 ->
 * +5 = 4, 001BA1A0(talk, 0x82C690); else 001831F0(2). 4: 001BA1F0(self)
 * nonzero -> D_008107F6 = 1, +5 = 0.
 * ---------------------------------------------------------------------- */
void l10_00825AB0(L10 *o, uint32_t self)
{
    uint32_t talk = self + 0x1F0;
    switch (l10_u8(o, self + 5)) {
    case 0:
        if (l10_u8(o, 0x00810702u) == 0xA) {
            l10_c_001BA1A0(o, talk, A19_SCRIPT_7A);
            l10_w8(o, self + 5, 1);
        }
        break;
    case 1:
        if (call_1BA1F0(o, self)) {
            l10_w8(o, self + 5, 2);
            l10_w16(o, self + 0x28, 0);
            l10_c_001831F0(o, 0);
            l10_c_001BA1A0(o, talk, A19_SCRIPT_7B);
            uint32_t b5 = l10_u8(o, 0x008102B5u);
            l10_w8(o, self + 7, b5);
            l10_w8(o, 0x008102B5u, 0);
        }
        l10_c_001831F0(o, 2);
        break;
    case 2: {
        if (call_1BA1F0(o, self)) l10_w8(o, self + 5, 3);
        int32_t t = l10_s16(o, self + 0x28);
        l10_w16(o, self + 0x28, (uint32_t)(t + 1));
        t = l10_s16(o, self + 0x28);
        if (t >= 0x1F4) {
            if (t == 0x1F4) {
                uint32_t b7 = l10_u8(o, self + 7);
                l10_w8(o, 0x008102B5u, b7);
            }
            l10_c_001831F0(o, 2);
        }
        break;
    }
    case 3:
        if (l10_u8(o, 0x008102B5u) == 0) {
            l10_w8(o, self + 5, 4);
            l10_c_001BA1A0(o, talk, A19_SCRIPT_7C);
        } else {
            l10_c_001831F0(o, 2);
        }
        break;
    case 4:
        if (call_1BA1F0(o, self)) {
            l10_w8(o, 0x008107F6u, 1);
            l10_w8(o, self + 5, 0);
        }
        break;
    default:
        break;
    }
}

int em_level10_port_00825AB0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    l10_00825AB0(&o, self);
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x8257C0: [7]. +4 0: 001B0F60(self, 0xB) zero -> +4 = 1, +8 = 3, +0x30
 * = 0x82C6D0, +0 = 2 when 001BA1C0(self, 0x1E) is nonzero, else 1. +4 1:
 * 0x825AB0 while D_008107F6 is 0, else 0x825930 (a hook); then, while
 * D_008107F6 (read again) is below 3, 0x700038A0 = (+0xB0, +0xB4, +0xB8,
 * 1) and 001F5940(7, 0x700038A0, 0); 001C68C0, 001B17A0 and the +0x4C
 * method. +4 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_008257C0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        l10_c_001B0F60(&o, self, 0xB, &r);
        if (r == 0) {
            l10_w8(&o, self + 4, 1);
            l10_w8(&o, self + 8, 3);
            l10_w32(&o, self + 0x30, A19_REC_7);
            l10_c_001BA1C0(&o, self, 0x1E, &r);
            l10_w8(&o, self, r ? 2 : 1);
        }
        break;
    case 1:
        if (l10_u8(&o, 0x008107F6u) == 0)
            l10_00825AB0(&o, self);
        else
            l10_c_00825930(&o, self);
        if (l10_u8(&o, 0x008107F6u) < 3u) {
            uint32_t x = l10_u32(&o, self + 0xB0);
            l10_w32(&o, S_700038A0, x);
            uint32_t y = l10_u32(&o, self + 0xB4);
            l10_w32(&o, S_700038A4, y);
            uint32_t z = l10_u32(&o, self + 0xB8);
            l10_w32(&o, S_700038A8, z);
            l10_w32(&o, S_700038AC, F_ONE);
            l10_c_001F5940(&o, 7, S_700038A0, 0);
        }
        l10_c_001C68C0(&o, self);
        (void)call_1B17A0(&o, self);
        l10_method(&o, self);
        break;
    case 2:
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825C70: [9]. +4 0: 001B0FD0(self) zero -> 001C6380, +0 = 1, +0x1F8 =
 * 0.057692308 (0x3D6C4EC5); D_00810776 0xFF -> +4 = 2, 0019C6F0(0x21, 0);
 * else +4 = 1, 0019C6F0(0x21, 1). +4 1 by +5: 0 -> when D_00810776 is 1,
 * +5 = 1 and +0x28 = 0; 1 -> with t the signed +0x28: t < 300 -> +0x28 =
 * t + 1; t < 560 -> at t == 300 0x700038B0 = 0, 0x700038A0 = (1012, 135,
 * 917.6, 1), 0x700038B4.. = (pi, 0, 1) and 001EFD90(2, 0x700038A0,
 * 0x700038B0); +0x28 += 1 (read again); with B = D_00275B40: (B+4)->+0x80
 * += +0x1F8, (B+8)->+0x80 += 2 * +0x1F8 (each re-read); 001C6380; t >= 560
 * and D_00810776 0xFF -> both +0x80 = 0 (B+8's first), +4 = 2,
 * 0019C6F0(0x21, 0), 001C6380. Then (any +5) the +0x4C method. +4 2: the
 * method. +4 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_00825C70(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        if (call_1B0FD0(&o, self)) break;
        l10_c_001C6380(&o, self);
        l10_w8(&o, self, 1);
        l10_w32(&o, self + 0x1F8, 0x3D6C4EC5u);
        if (l10_u8(&o, 0x00810776u) == 0xFF) {
            l10_w8(&o, self + 4, 2);
            l10_c_0019C6F0(&o, 0x21, 0, &r);
        } else {
            l10_w8(&o, self + 4, 1);
            l10_c_0019C6F0(&o, 0x21, 1, &r);
        }
        break;
    case 1: {
        uint32_t sub = l10_u8(&o, self + 5);
        if (sub == 1) {
            int32_t t = l10_s16(&o, self + 0x28);
            if (t < 300) {
                l10_w16(&o, self + 0x28, (uint32_t)(t + 1));
            } else if (t < 0x230) {
                if (t == 300) {
                    l10_w32(&o, S_700038B0, F_ZERO);
                    l10_w32(&o, S_700038A0, 0x447D0000u); /* 1012 */
                    l10_w32(&o, S_700038A4, 0x43070000u); /* 135 */
                    l10_w32(&o, S_700038A8, 0x44656666u); /* 917.6 */
                    l10_w32(&o, S_700038AC, F_ONE);
                    l10_w32(&o, S_700038B4, F_PI);
                    l10_w32(&o, S_700038B8, F_ZERO);
                    l10_w32(&o, S_700038BC, F_ONE);
                    l10_c_001EFD90(&o, 2, S_700038A0, S_700038B0);
                }
                int32_t again = l10_s16(&o, self + 0x28);
                l10_w16(&o, self + 0x28, (uint32_t)(again + 1));
                uint32_t blk = l10_u32(&o, D_00275B40);
                uint32_t step = l10_u32(&o, self + 0x1F8);
                uint32_t a = l10_u32(&o, blk + 4);
                uint32_t v = l10_u32(&o, a + 0x80);
                l10_w32(&o, a + 0x80, L10_ADD(v, step));
                blk = l10_u32(&o, D_00275B40);
                step = l10_u32(&o, self + 0x1F8);
                uint32_t b = l10_u32(&o, blk + 8);
                uint32_t twice = L10_MUL(F_2, step);
                v = l10_u32(&o, b + 0x80);
                l10_w32(&o, b + 0x80, L10_ADD(v, twice));
                l10_c_001C6380(&o, self);
            } else if (l10_u8(&o, 0x00810776u) == 0xFF) {
                uint32_t blk = l10_u32(&o, D_00275B40);
                uint32_t b = l10_u32(&o, blk + 8);
                l10_w32(&o, b + 0x80, F_ZERO);
                blk = l10_u32(&o, D_00275B40);
                uint32_t a = l10_u32(&o, blk + 4);
                l10_w32(&o, a + 0x80, F_ZERO);
                l10_w8(&o, self + 4, 2);
                l10_c_0019C6F0(&o, 0x21, 0, &r);
                l10_c_001C6380(&o, self);
            }
        } else if (sub == 0) {
            if (l10_u8(&o, 0x00810776u) == 1) {
                l10_w8(&o, self + 5, 1);
                l10_w16(&o, self + 0x28, 0);
            }
        }
        l10_method(&o, self);
        break;
    }
    case 2:
        l10_method(&o, self);
        break;
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x825EE0: [47]. +4 0: 0x700038A0 = (1, 0, 0, 0.25); D_00810776 0xFF ->
 * +0xD = 0x1F and, when 001B0FD0(self) is 0, 001C6380, +4 = 2, +0x1F0 =
 * 001C5570(self, 0x700038A0, 0x24, 0); else, when 001B0FD0 is 0,
 * 001C6380, +4 = 1, +0x1F0 = 001C5570(.., 0x23, 0). +4 1: when D_00810776
 * is 0xFF, or else D_008107F6 is 6 or more: 001CA6E0(self, 001C6120(
 * D_0028A59C, 0x1F)), 001C62C0, 001C6380, +4 = 2, and with +0x1F0 set its
 * +4 = 3, 0x700038A0 = (1, 0, 0, 0.25), +0x1F0 = 001C5570(.., 0x24, 0).
 * Then (and in +4 2) the +0x4C method when 001B17A0 is nonzero. +4 3:
 * 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_00825EE0(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    uint32_t child = 0;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        put_vec(&o, F_ONE, F_ZERO, F_ZERO, F_QUARTER);
        if (l10_u8(&o, 0x00810776u) == 0xFF) {
            l10_w8(&o, self + 0xD, 0x1F);
            if (call_1B0FD0(&o, self) == 0) {
                l10_c_001C6380(&o, self);
                l10_w8(&o, self + 4, 2);
                l10_c_001C5570(&o, self, S_700038A0, 0x24, 0, &child);
                l10_w32(&o, self + 0x1F0, child);
            }
        } else if (call_1B0FD0(&o, self) == 0) {
            l10_c_001C6380(&o, self);
            l10_w8(&o, self + 4, 1);
            l10_c_001C5570(&o, self, S_700038A0, 0x23, 0, &child);
            l10_w32(&o, self + 0x1F0, child);
        }
        break;
    case 1: {
        int go = l10_u8(&o, 0x00810776u) == 0xFF;
        if (!go) go = l10_u8(&o, 0x008107F6u) >= 6u;
        if (go) {
            uint32_t table = l10_u32(&o, D_0028A59C);
            uint32_t model = 0;
            l10_c_001C6120(&o, table, 0x1F, &model);
            l10_c_001CA6E0(&o, self, model);
            l10_c_001C62C0(&o, self);
            l10_c_001C6380(&o, self);
            l10_w8(&o, self + 4, 2);
            uint32_t old = l10_u32(&o, self + 0x1F0);
            if (old) {
                l10_w8(&o, old + 4, 3);
                put_vec(&o, F_ONE, F_ZERO, F_ZERO, F_QUARTER);
                l10_c_001C5570(&o, self, S_700038A0, 0x24, 0, &child);
                l10_w32(&o, self + 0x1F0, child);
            }
        }
        if (call_1B17A0(&o, self)) l10_method(&o, self);
        break;
    }
    case 2:
        if (call_1B17A0(&o, self)) l10_method(&o, self);
        break;
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x826100: [18]. +4 0: 001B0FD0(self) zero -> 001C6380, +0 = 1, +4 = 3
 * when D_00810777 is 0xFF else 4, +0x2E8 = +0x2EC = 0. +4 4: the player
 * at x (0x810350) in (962, 968) [x <= 962 out, x < 968 in], y not <= 293,
 * z in (923.6, 929.6) [z <= 923.6 out, z < 929.6 in] and D_008104A0 ==
 * 0x2A -> 001BA1A0(talk, 0x82C6E0), +4 = 1; then the +0x4C method when
 * 001B17A0 is nonzero. +4 1: z not <= 796.589 -> while the signed +0x28 is
 * below 31 it counts up, else z = z - 1 (the z read before); then (z read
 * again) z < 916.321 and not <= 810.023 -> y -= 0.1597114. z (read again)
 * < 808 and D_00810777 0 -> 001FC580(self, 0x19D), 001B1E20(7, 0x28),
 * 001EFD90(0x8000000A / 0x80000015 / 0x8000000B, self + 0xB0, self +
 * 0xC0), D_00810777 = 0xFF. 001BA1F0(self) nonzero -> +4 = 3; with
 * D_00810777 0 the +0x4C method when 001B17A0 is nonzero. +4 3: 001AFC10.
 * Other +4 (2, above 4): nothing.
 * ---------------------------------------------------------------------- */
int em_level10_port_00826100(const EmLevel10PortHooks *h, uint32_t self, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    switch (l10_u8(&o, self + 4)) {
    case 0:
        if (call_1B0FD0(&o, self)) break;
        l10_c_001C6380(&o, self);
        l10_w8(&o, self, 1);
        l10_w8(&o, self + 4, l10_u8(&o, 0x00810777u) == 0xFF ? 3 : 4);
        l10_w32(&o, self + 0x2E8, 0);
        l10_w32(&o, self + 0x2EC, 0);
        break;
    case 4: {
        uint32_t x = l10_u32(&o, D_00810350);
        if (!L10_LE(x, 0x44708000u) && L10_LT(x, 0x44720000u)) {          /* 962, 968 */
            uint32_t y = l10_u32(&o, D_00810354);
            if (!L10_LE(y, 0x43928000u)) {                                  /* 293 */
                uint32_t z = l10_u32(&o, D_00810358);
                if (!L10_LE(z, 0x4466E666u) && L10_LT(z, 0x44686666u)) {    /* 923.6, 929.6 */
                    if (l10_u8(&o, 0x008104A0u) == 0x2A) {
                        l10_c_001BA1A0(&o, self + 0x1F0, A19_SCRIPT_18);
                        l10_w8(&o, self + 4, 1);
                    }
                }
            }
        }
        if (call_1B17A0(&o, self)) l10_method(&o, self);
        break;
    }
    case 1: {
        uint32_t z = l10_u32(&o, D_00810358);
        if (!L10_LE(z, 0x444725B2u)) {                                      /* 796.589 */
            int32_t t = l10_s16(&o, self + 0x28);
            if (t < 0x1F)
                l10_w16(&o, self + 0x28, (uint32_t)(t + 1));
            else
                l10_w32(&o, D_00810358, L10_SUB(z, F_ONE));
            z = l10_u32(&o, D_00810358);
            if (L10_LT(z, 0x4465148Bu) && !L10_LE(z, 0x444A8179u)) {        /* 916.321, 810.023 */
                uint32_t y = l10_u32(&o, D_00810354);
                l10_w32(&o, D_00810354, L10_SUB(y, 0x3E238B63u));          /* 0.1597114 */
            }
        }
        z = l10_u32(&o, D_00810358);
        if (L10_LT(z, 0x444A0000u)) {                                       /* 808 */
            if (l10_u8(&o, 0x00810777u) == 0) {
                l10_c_001FC580(&o, self, 0x19D);
                l10_c_001B1E20(&o, 7, 0x28);
                l10_c_001EFD90(&o, (int32_t)0x8000000Au, self + 0xB0, self + 0xC0);
                l10_c_001EFD90(&o, (int32_t)0x80000015u, self + 0xB0, self + 0xC0);
                l10_c_001EFD90(&o, (int32_t)0x8000000Bu, self + 0xB0, self + 0xC0);
                l10_w8(&o, 0x00810777u, 0xFF);
            }
        }
        if (call_1BA1F0(&o, self)) l10_w8(&o, self + 4, 3);
        if (l10_u8(&o, 0x00810777u) == 0) {
            if (call_1B17A0(&o, self)) l10_method(&o, self);
        }
        break;
    }
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x827790 (self; sp: the stack pointer at its entry; its frame is 0x70
 * and holds the area record at sp - 0x40): [10]. At entry, every call: the
 * 64-byte area 0x82E050 copied to the frame. +4 0: +4 = 1, +0 = 1. +4 1 by
 * D_0081081D (other values: nothing):
 *   0, by +5: 0 -> when D_00810702 is 0xD, 001BA1A0(talk, 0x82D990), +5 =
 *     1, +0x28 = 0; 1 -> 001BA1F0(self) (its result unused), 001831F0(2),
 *     +0x28 += 1 (signed, read again) and from 366 on 001BA1A0(talk,
 *     0x82DA10), +5 = 2; 2 -> 001BA1F0 nonzero: 001C4760(0xC, 1), +5 = 0,
 *     D_0081081D = 0x80.
 *   0x80, by +5: 0 -> 001B1EA0(0, 0x810350, the copy, 4) == 1:
 *     001BA1A0(talk, 0x82DD10), +5 = 1; 1 -> 001BA1F0 nonzero: +0x2A = 0,
 *     +5 = 2; 2 -> 001B1EA0(..) == 0: +5 = 3; 3 -> +0x2A += 1 (read again)
 *     and from 601 on +5 = 0.
 * +4 2 / 3: 001AFC10.
 * ---------------------------------------------------------------------- */
int em_level10_port_00827790(const EmLevel10PortHooks *h, uint32_t self, uint32_t sp, EmLevel10PortFault *fault)
{
    L10 o;
    if (l10_begin(&o, h, fault)) return -1;
    uint32_t area = sp - 0x40;
    uint32_t talk = self + 0x1F0;
    int32_t r = 0;
    copy_area(&o, A19_AREA_10, area);
    switch (l10_u8(&o, self + 4)) {
    case 0:
        l10_w8(&o, self + 4, 1);
        l10_w8(&o, self, 1);
        break;
    case 1: {
        uint32_t k = l10_u8(&o, 0x0081081Du);
        if (k == 0x80) {
            switch (l10_u8(&o, self + 5)) {
            case 3: {
                int32_t t = l10_s16(&o, self + 0x2A);
                l10_w16(&o, self + 0x2A, (uint32_t)(t + 1));
                if (l10_s16(&o, self + 0x2A) >= 0x259) l10_w8(&o, self + 5, 0);
                break;
            }
            case 2:
                l10_c_001B1EA0(&o, 0, D_00810350, area, 4, &r);
                if (r == 0) l10_w8(&o, self + 5, 3);
                break;
            case 1:
                if (call_1BA1F0(&o, self)) {
                    l10_w16(&o, self + 0x2A, 0);
                    l10_w8(&o, self + 5, 2);
                }
                break;
            case 0:
                l10_c_001B1EA0(&o, 0, D_00810350, area, 4, &r);
                if (r == 1) {
                    l10_c_001BA1A0(&o, talk, A19_SCRIPT_10C);
                    l10_w8(&o, self + 5, 1);
                }
                break;
            default:
                break;
            }
        } else if (k == 0) {
            switch (l10_u8(&o, self + 5)) {
            case 2:
                if (call_1BA1F0(&o, self)) {
                    int32_t ignored = 0;
                    l10_c_001C4760(&o, 0xC, 1, &ignored);
                    l10_w8(&o, self + 5, 0);
                    l10_w8(&o, 0x0081081Du, 0x80);
                }
                break;
            case 1: {
                (void)call_1BA1F0(&o, self);
                l10_c_001831F0(&o, 2);
                int32_t t = l10_s16(&o, self + 0x28);
                l10_w16(&o, self + 0x28, (uint32_t)(t + 1));
                if (l10_s16(&o, self + 0x28) >= 0x16E) {
                    l10_c_001BA1A0(&o, talk, A19_SCRIPT_10B);
                    l10_w8(&o, self + 5, 2);
                }
                break;
            }
            case 0:
                if (l10_u8(&o, 0x00810702u) == 0xD) {
                    l10_c_001BA1A0(&o, talk, A19_SCRIPT_10A);
                    l10_w8(&o, self + 5, 1);
                    l10_w16(&o, self + 0x28, 0);
                }
                break;
            default:
                break;
            }
        }
        break;
    }
    case 2:
    case 3:
        l10_c_001AFC10(&o, self);
        break;
    default:
        break;
    }
    return l10_end(&o);
}

/* ------------------------------------------------------------------------
 * 0x829370 (self, matrix; sp: the stack pointer at its entry, frame 0x80):
 * the probe of the group owner 0x827DD0 (AREA19's twin of AREA01 0x8282F0:
 * the same instructions but the tint record's address; the shape of
 * em_level9_port_turret.c's 0x826140, AREA13's twin, read again here from
 * the AREA19 instructions). Stack locals: dir = sp - 0x20, tint = sp -
 * 0x10.
 *  - 0x700038A0 = 60 (stored first), dir = (3, -2, 0, 1), then 38A8, 38A4,
 *    38AC = 0; 001026A0(0x700038A0, matrix, 0x700038A0); 001026A0(dir,
 *    matrix, dir); 001028B8(0x700038A0, 0x700038A0, dir); 38AC = 1.
 *  - hit = 2 when 0019AA80(dir, 0x700038A0, 0x20) is nonzero (then
 *    00102948(0x700038A0, 0x700031B0)), else 0.
 *  - The words 0x700031D8, D4, D0 are read; when 0019A570(dir,
 *    0x700038A0, 7, 0x20) is nonzero: 001028D0(0x700038A0, 0x700031B0,
 *    D_00810360), 38AC = 0, d = 00102738(0x700038A0, 0x700038A0),
 *    0x70003A20 = d; hit = 1 when d is not <= 10000, else 1 when 31D8 ==
 *    1 and byte +3 of the record at 31D4 is 0x10..0x13, else 2. When it
 *    is zero the three words are stored back (D8, D4, D0).
 *  - hit 2: self +0x204 = 31D4 when it is 0 and 31D8 == 1. Then +4 == 4:
 *    001031E0(0x700038A0, 0x700031B0), 38AC = 1, the random colour
 *    (((r >> 16) * 0xFFFF >> 15 >> 15) & 0x1F) + 0x40 into 38B0 with 38B4
 *    = 38B8 = 0, 38BC = 0x80, the four read back (BC, B8, B4, B0) into
 *    rgba, 001CD520(0, 2, 0x700038A0, the GIF tag, rgba, 3, 3, 2), 38C0 =
 *    0.8, 38C8 = 38C4 = 0, 001E2BA0(dir, 0x700038A0, 0x700038C0, 100);
 *    otherwise when the signed +0x200 is 13 or more: tint = the overlay's
 *    0x82F670 (one quadword), 001031E0(0x700038A0, 0x700031B0), 38AC = 1
 *    (after reading D_00275B40), 001026A0(0x700038C0, (D_00275B40 +
 *    0xC)->+0x90, tint), 38CC = 1, 38D8 = 38D4 = 38D0 = 0.8,
 *    001E2BA0(0x700038A0, 0x700038C0, 0x700038D0, 100).
 *  - hit 1: 001031E0(0x700038B0, 0x700031B0), 38BC = 1, 38C0 = 0.8, 38C8
 *    = 38C4 = 0, 001E2BA0(dir, 0x700038B0, 0x700038C0, 100).
 *  Returns 0 unless hit is 2; then 2 when self +0x204 == 31D4 (both read
 *  again), else 1.
 * ---------------------------------------------------------------------- */
#define GIFTAG_829370 UINT64_C(0x20045BA5154222DC)
#define K_M2 0xC0000000u      /* -2.0f */
#define K_0_8 0x3F4CCCCDu     /* 0.8f */
#define K_10000 0x461C4000u   /* 10000.0f */

static int32_t probe(L10 *o, uint32_t self, uint32_t matrix, uint32_t sp)
{
    uint32_t dir = sp - 0x20, tint = sp - 0x10;
    int32_t r = 0;
    int hit;

    l10_w32(o, S_700038A0, F_60);
    l10_w32(o, dir + 0x0, F_3);
    l10_w32(o, dir + 0x4, K_M2);
    l10_w32(o, dir + 0x8, F_ZERO);
    l10_w32(o, dir + 0xC, F_ONE);
    l10_w32(o, S_700038A8, F_ZERO);
    l10_w32(o, S_700038A4, F_ZERO);
    l10_w32(o, S_700038AC, F_ZERO);
    l10_c_001026A0(o, S_700038A0, matrix, S_700038A0);
    l10_c_001026A0(o, dir, matrix, dir);
    l10_c_001028B8(o, S_700038A0, S_700038A0, dir);
    l10_w32(o, S_700038AC, F_ONE);
    l10_c_0019AA80(o, dir, S_700038A0, 0x20, &r);
    if (r != 0) {
        l10_c_00102948(o, S_700038A0, S_700031B0);
        hit = 2;
    } else {
        hit = 0;
    }
    uint32_t save_d8 = l10_u32(o, S_700031D8);
    uint32_t save_d4 = l10_u32(o, S_700031D4);
    uint32_t save_d0 = l10_u32(o, S_700031D0);
    r = 0;
    l10_c_0019A570(o, dir, S_700038A0, 7, 0x20, &r);
    if (r != 0) {
        uint32_t d = 0;
        l10_c_001028D0(o, S_700038A0, S_700031B0, D_00810360);
        l10_w32(o, S_700038AC, F_ZERO);
        l10_c_00102738(o, S_700038A0, S_700038A0, &d);
        l10_w32(o, S_70003A20, d);
        if (!L10_LE(d, K_10000)) {
            hit = 1;
        } else if (l10_u32(o, S_700031D8) != 1) {
            hit = 2;
        } else {
            uint32_t kind = l10_u8(o, l10_u32(o, S_700031D4) + 3);
            hit = (kind >= 0x10 && kind < 0x14) ? 1 : 2;
        }
    } else {
        l10_w32(o, S_700031D8, save_d8);
        l10_w32(o, S_700031D4, save_d4);
        l10_w32(o, S_700031D0, save_d0);
    }

    if (hit == 2) {
        if (l10_u32(o, self + 0x204) == 0 && l10_u32(o, S_700031D8) == 1) {
            uint32_t other = l10_u32(o, S_700031D4);
            l10_w32(o, self + 0x204, other);
        }
        if (l10_u8(o, self + 4) == 4) {
            l10_c_001031E0(o, S_700038A0, S_700031B0);
            l10_w32(o, S_700038AC, F_ONE);
            r = 0;
            l10_c_00122BB8(o, &r);
            int32_t x = l10_sra((uint32_t)r, 16);
            x = l10_sra(((uint32_t)x << 16) - (uint32_t)x, 15);
            l10_w32(o, S_700038B0, (uint32_t)((l10_sra((uint32_t)x, 15) & 0x1F) + 0x40));
            l10_w32(o, S_700038B4, 0);
            l10_w32(o, S_700038B8, 0);
            l10_w32(o, S_700038BC, 0x80);
            uint32_t bc = l10_u32(o, S_700038BC);
            uint32_t b8 = l10_u32(o, S_700038B8);
            uint32_t b4 = l10_u32(o, S_700038B4);
            uint32_t b0 = l10_u32(o, S_700038B0);
            uint32_t rgba = b0 | (b4 << 8 | (bc << 24 | b8 << 16));
            r = 0;
            l10_c_001CD520(o, 0, 2, S_700038A0, GIFTAG_829370, rgba, F_3, F_3, F_2, &r);
            l10_w32(o, S_700038C0, K_0_8);
            l10_w32(o, S_700038C8, F_ZERO);
            l10_w32(o, S_700038C4, F_ZERO);
            l10_c_001E2BA0(o, dir, S_700038A0, S_700038C0, F_100);
        } else if (l10_s32(o, self + 0x200) >= 0xD) {
            uint8_t q[16];
            l10_q(o, A19_TINT, q);
            l10_wq(o, tint, q);
            l10_c_001031E0(o, S_700038A0, S_700031B0);
            uint32_t table = l10_u32(o, D_00275B40);
            l10_w32(o, S_700038AC, F_ONE);
            uint32_t record = l10_u32(o, table + 0xC);
            l10_c_001026A0(o, S_700038C0, record + 0x90, tint);
            l10_w32(o, S_700038CC, F_ONE);
            l10_w32(o, S_700038D0 + 8, K_0_8);
            l10_w32(o, S_700038D0 + 4, K_0_8);
            l10_w32(o, S_700038D0, K_0_8);
            l10_c_001E2BA0(o, S_700038A0, S_700038C0, S_700038D0, F_100);
        }
    } else if (hit == 1) {
        l10_c_001031E0(o, S_700038B0, S_700031B0);
        l10_w32(o, S_700038BC, F_ONE);
        l10_w32(o, S_700038C0, K_0_8);
        l10_w32(o, S_700038C8, F_ZERO);
        l10_w32(o, S_700038C4, F_ZERO);
        l10_c_001E2BA0(o, dir, S_700038B0, S_700038C0, F_100);
    }
    if (hit != 2) return 0;
    uint32_t mine = l10_u32(o, self + 0x204);
    uint32_t other = l10_u32(o, S_700031D4);
    return mine == other ? 2 : 1;
}

int em_level10_port_00829370(const EmLevel10PortHooks *h, uint32_t self, uint32_t matrix, uint32_t sp,
                             int32_t *result, EmLevel10PortFault *fault)
{
    L10 o;
    if (!result || l10_begin(&o, h, fault)) return -1;
    int32_t r = probe(&o, self, matrix, sp);
    if (l10_failed(&o)) return -1;
    *result = r;
    return 0;
}
