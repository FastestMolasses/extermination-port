/* The fourteenth level: the two AREA19 functions the a19d census found new
 * (overlay id 16; runtime addresses): the callbacks of the lift's script
 * (0x826C10's script 0x82D290, run in a19d_07 when the lift drops).
 * docs/LEVEL14_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level14_port_internal.h"

/* ------------------------------------------------------------------------
 * 0x826B30 (self): three 001EFD90(0x80000015, 0x700038A0, self + 0xC0) at
 * (855.9, 370, 863), (850.9, 370, 856) and (863.9, 370, 876.3), w = 1;
 * returns 1.
 * ---------------------------------------------------------------------- */
int em_level14_port_00826B30(const H14 *h, uint32_t self, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    l14_w32(&o, 0x700038ACu, 0x3F800000u);
    l14_w32(&o, 0x700038A4u, 0x43B90000u);
    l14_w32(&o, 0x700038A0u, 0x4455F99Au);
    l14_w32(&o, 0x700038A8u, 0x4457C000u);
    if (l14_c_001EFD90(&o, (int32_t)0x80000015u, 0x700038A0u, self + 0xC0)) return -1;
    l14_w32(&o, 0x700038A0u, 0x4454B99Au);
    l14_w32(&o, 0x700038A8u, 0x44560000u);
    if (l14_c_001EFD90(&o, (int32_t)0x80000015u, 0x700038A0u, self + 0xC0)) return -1;
    l14_w32(&o, 0x700038A0u, 0x4457F99Au);
    l14_w32(&o, 0x700038A8u, 0x445B1333u);
    if (l14_c_001EFD90(&o, (int32_t)0x80000015u, 0x700038A0u, self + 0xC0)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 0x827540 (a0): the halfword a0 +0x28 = 1 (the lift's drop flag, read by
 * 0x826C10 state 1); returns 1.
 * ---------------------------------------------------------------------- */
int em_level14_port_00827540(const H14 *h, uint32_t a0, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    l14_w16(&o, a0 + 0x28, 1);
    if (l14_failed(&o)) return -1;
    *result = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * Script 0x82C6E0's op09 callbacks (byte-identical C):
 * 0x826470 (record 0x82C9A0): +0x3C of the player at 23 or under: sound 0x134
 * once (D_00275B08 = 1), then D_008102BA != 0; else 0.
 * 0x826500 (record 0x82C7A0): +0x2EC = 001FBD50(self, 0x135, 0, 300); 1.
 * 0x826540 (record 0x82C8A0): 0011A070(+0x2EC) unless it is -1; 1.
 * ---------------------------------------------------------------------- */
int em_level14_port_00826470(const H14 *h, uint32_t self, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    int32_t r = 0;
    if (L14_LE(l14_u32(&o, 0x008102ECu), 0x41B80000u)) {
        if ((int32_t)l14_u32(&o, 0x00275B08u) != 1) {
            if (l14_c_001FBD50(&o, self, 0x134, 0, 0x43960000u)) return -1;
            l14_w32(&o, 0x00275B08u, 1);
        }
        r = l14_u8(&o, 0x008102BAu) != 0;
    }
    if (l14_end(&o) < 0) return -1;
    *result = r;
    return 0;
}

int em_level14_port_00826500(const H14 *h, uint32_t self, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    if (l14_c_001FBD50(&o, self, 0x135, 0, 0x43960000u)) return -1;
    l14_w32(&o, self + 0x2EC, (uint32_t)o.sound_result);
    if (l14_end(&o) < 0) return -1;
    *result = 1;
    return 0;
}

int em_level14_port_00826540(const H14 *h, uint32_t self, int32_t *result, F14 *fault)
{
    L14 o;
    if (!result || l14_begin(&o, h, fault)) return -1;
    const int32_t handle = (int32_t)l14_u32(&o, self + 0x2EC);
    if (handle != -1 && l14_c_0011A070(&o, handle)) return -1;
    if (l14_end(&o) < 0) return -1;
    *result = 1;
    return 0;
}
