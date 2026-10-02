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
