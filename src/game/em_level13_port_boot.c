/* The thirteenth level: the boot functions the a19c census found new that
 * are neither 001386E0's behaviours nor the tendrils: the AREA19 bar-pad
 * test 00194240, the sub-change camera set-up 00197390, the probe chain
 * 001B2B80 / 001B3250 / 001B37D0, the box test 001B34F0, the draw method
 * 001CB1F0 / 001CB140 with its DMA reference tags 001D4430 / 001D42E0,
 * and the light-record helpers 001F6AC0 / 001F6B90.
 * docs/LEVEL13_PORT.md section 2. Ground truth: the original instructions
 * (the test runs every entry against them). */
#include "em_level13_port_internal.h"

#define F_ONE 0x3F800000u
#define F_PI 0x40490FDBu

/* ------------------------------------------------------------------------
 * 00194240 (p) -> 1 when the position p +0xA0 / +0xA4 / +0xA8 is at one of
 * four AREA19 points: within 15 horizontally (dx^2 + dz^2 < 225, MULA /
 * MADD) and |y - h| < 4 (0011DF78): (1017.9, 223, 1030.9) -> D_008106F2 =
 * 6; (997.2, 198, 929.8) -> 1; (896.5, 197, 930.2) -> 2; (760.7, 268,
 * 883.1) -> 3. The first match wins; 0 otherwise.
 * ---------------------------------------------------------------------- */
static const uint32_t L13_PADS[4][4] = {
    {0x447E799Au, 0x4480DCCDu, 0x435F0000u, 6},
    {0x44794CCDu, 0x44687333u, 0x43460000u, 1},
    {0x44602000u, 0x44688CCDu, 0x43450000u, 2},
    {0x443E2CCDu, 0x445CC666u, 0x43860000u, 3},
};

int em_level13_port_00194240(const H13 *h, uint32_t p, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    int32_t found = 0;
    for (int i = 0; i < 4 && !found; i++) {
        uint32_t x = l13_u32(&o, p + 0xA0);
        uint32_t z = l13_u32(&o, p + 0xA8);
        uint32_t dx = L13_SUB(x, L13_PADS[i][0]);
        uint32_t dz = L13_SUB(z, L13_PADS[i][1]);
        if (!L13_LT(L13_MADD(L13_MULA(dx, dx), dz, dz), 0x43610000u)) continue;
        uint32_t a = 0;
        if (l13_c_0011DF78(&o, L13_SUB(l13_u32(&o, p + 0xA4), L13_PADS[i][2]), &a)) return -1;
        if (L13_LT(a, 0x40800000u)) {
            l13_w8(&o, 0x008106F2u, L13_PADS[i][3]);
            found = 1;
        }
    }
    if (l13_failed(&o)) return -1;
    *result = found;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00197390 (cam, other): 001916C0(cam, other, 0); the point cam +0x10 /
 * +0x14 / +0x18 = row D_00810702 of the table D_0024A6C8 (12 bytes a row;
 * D_00810702 read for each word); 0018D7B0(cam, 5); 00102948(0x8105D0,
 * cam +0x10); unless other +0x230 is 9, 8 or 0x2C: cam +6 = +1 = +2 = 0.
 * ---------------------------------------------------------------------- */
int em_level13_port_00197390(const H13 *h, uint32_t cam, uint32_t other, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    if (l13_c_001916C0(&o, cam, other, 0)) return -1;
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t row = l13_u8(&o, 0x00810702u);
        l13_w32(&o, cam + 0x10 + 4 * k, l13_u32(&o, 0x0024A6C8u + 4 * k + row * 12u));
    }
    if (l13_c_0018D7B0(&o, cam, 5)) return -1;
    if (l13_c_00102948(&o, 0x008105D0u, cam + 0x10)) return -1;
    uint32_t s = l13_u32(&o, other + 0x230);
    if (s != 9 && s != 8 && s != 0x2C) {
        l13_w8(&o, cam + 6, 0);
        l13_w8(&o, cam + 1, 0);
        l13_w8(&o, cam + 2, 0);
    }
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 001B2B80 (a0, a1, a2): a1 +0xC = 1.0; 0019AD00(a0, a1, 7) set ->
 * 001B2D00(a1, a2) | 4, else 001B2F70(a1, a2).
 * ---------------------------------------------------------------------- */
int32_t l13_001B2B80(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    int32_t r = 0;
    l13_w32(o, a1 + 0xC, F_ONE);
    if (l13_c_0019AD00(o, a0, a1, 7, &r)) return 0;
    if (r != 0) {
        if (l13_c_001B2D00(o, a1, a2, &r)) return 0;
        return r | 4;
    }
    if (l13_c_001B2F70(o, a1, a2, &r)) return 0;
    return r;
}

int em_level13_port_001B2B80(const H13 *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    int32_t r = l13_001B2B80(&o, a0, a1, a2);
    if (l13_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B3250 (self, point, lim, sp): r = 001B2B80(self, point, the frame word
 * sp - 0x10). Bit 0 clear -> 2. Bit 2 set with the hit object's
 * (*0x700031D0) halfword +0x1A bit 0x2000 -> 1. Otherwise self +0xB4 - lim
 * below the frame word -> 0, else 2.
 * ---------------------------------------------------------------------- */
int32_t l13_001B3250(L13 *o, uint32_t self, uint32_t point, uint32_t lim, uint32_t sp)
{
    int32_t r = l13_001B2B80(o, self, point, sp - 0x10u);
    if (l13_failed(o)) return 0;
    if (!(r & 1)) return 2;
    if (r & 4) {
        if (l13_u16(o, l13_u32(o, 0x700031D0u) + 0x1A) & 0x2000u) return 1;
    }
    uint32_t y = L13_SUB(l13_u32(o, self + 0xB4), lim);
    return L13_LT(y, l13_u32(o, sp - 0x10u)) ? 0 : 2;
}

int em_level13_port_001B3250(const H13 *h, uint32_t self, uint32_t point, float lim, uint32_t sp, int32_t *result,
                             F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    int32_t r = l13_001B3250(&o, self, point, l13_bits(lim), sp);
    if (l13_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B34F0 (a0, a1, a2) -> 1 when a2 lies in the box around a0: the
 * horizontal distance squared (MULA / MADD) not above a1 +0 squared and
 * |a2 y - a0 y| (0011DF78) not above a1 +4.
 * ---------------------------------------------------------------------- */
int32_t l13_001B34F0(L13 *o, uint32_t a0, uint32_t a1, uint32_t a2)
{
    uint32_t x2 = l13_u32(o, a2);
    uint32_t x0 = l13_u32(o, a0);
    uint32_t r = l13_u32(o, a1);
    uint32_t z2 = l13_u32(o, a2 + 8);
    uint32_t z0 = l13_u32(o, a0 + 8);
    uint32_t dx = L13_SUB(x2, x0);
    uint32_t rr = L13_MUL(r, r);
    uint32_t dz = L13_SUB(z2, z0);
    if (L13_LT(rr, L13_MADD(L13_MULA(dx, dx), dz, dz))) return 0;
    uint32_t y2 = l13_u32(o, a2 + 4);
    uint32_t y0 = l13_u32(o, a0 + 4);
    uint32_t d = 0;
    if (l13_c_0011DF78(o, L13_SUB(y2, y0), &d)) return 0;
    return L13_LE(d, l13_u32(o, a1 + 4)) ? 1 : 0;
}

int em_level13_port_001B34F0(const H13 *h, uint32_t a0, uint32_t a1, uint32_t a2, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    int32_t r = l13_001B34F0(&o, a0, a1, a2);
    if (l13_failed(&o)) return -1;
    *result = r;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B37D0 (self, dist, lim, sp) -> a heading: a = 001B1470(pi + the yaw);
 * the probe point = (0, 3, dist, 1) (the frame vector at sp - 0x10) turned
 * by a (0x70003400 identity, 00102BB0) into 0x70003600 plus the position;
 * 001B3250(self, 0x70003600, lim) zero -> a. Otherwise for s 1..7 and each
 * side (minus, then plus s pi/8 from pi + the yaw, 001B1470) the same
 * probe; the first heading whose probe returns 0 is the result, else the
 * last one tried.
 * ---------------------------------------------------------------------- */
static int32_t l13_37D0_probe(L13 *o, uint32_t self, uint32_t ang, uint32_t lim, uint32_t sp)
{
    if (l13_c_001029C0(o, 0x70003400u)) return -1;
    if (l13_c_00102BB0(o, 0x70003400u, 0x70003400u, ang)) return -1;
    if (l13_c_001026A0(o, 0x70003600u, 0x70003400u, sp - 0x10u)) return -1;
    if (l13_c_001028B8(o, 0x70003600u, 0x70003600u, self + 0xB0)) return -1;
    int32_t r = l13_001B3250(o, self, 0x70003600u, lim, sp - 0x60u);
    return l13_failed(o) ? -1 : r;
}

static uint32_t l13_001B37D0(L13 *o, uint32_t self, uint32_t dist, uint32_t lim, uint32_t sp)
{
    uint32_t ang = 0;
    if (l13_c_001B1470(o, L13_ADD(F_PI, l13_u32(o, self + 0xC4)), &ang)) return 0;
    if (l13_c_001029C0(o, 0x70003400u)) return 0;
    if (l13_c_00102BB0(o, 0x70003400u, 0x70003400u, ang)) return 0;
    l13_w32(o, sp - 0x10u, 0);
    l13_w32(o, sp - 0x0Cu, 0x40400000u);
    l13_w32(o, sp - 0x08u, dist);
    l13_w32(o, sp - 0x04u, F_ONE);
    if (l13_c_001026A0(o, 0x70003600u, 0x70003400u, sp - 0x10u)) return 0;
    if (l13_c_001028B8(o, 0x70003600u, 0x70003600u, self + 0xB0)) return 0;
    int32_t r = l13_001B3250(o, self, 0x70003600u, lim, sp - 0x60u);
    if (l13_failed(o) || r == 0) return ang;
    for (int32_t s = 0; s < 7; s++) {
        uint32_t off = L13_MUL(0x3EC90FDBu, L13_CVT_S_W((uint32_t)(s + 1)));
        for (int32_t side = 0; side < 2; side++) {
            uint32_t base = L13_ADD(F_PI, l13_u32(o, self + 0xC4));
            if (l13_c_001B1470(o, (side & 1) ? L13_ADD(base, off) : L13_SUB(base, off), &ang)) return 0;
            r = l13_37D0_probe(o, self, ang, lim, sp);
            if (r <= 0) return ang;
        }
    }
    return ang;
}

int em_level13_port_001B37D0(const H13 *h, uint32_t self, float dist, float lim, uint32_t sp, float *result,
                             F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    uint32_t a = l13_001B37D0(&o, self, l13_bits(dist), l13_bits(lim), sp);
    if (l13_failed(&o)) return -1;
    *result = l13_float(a);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001D42E0 (ch, model): the render-state calls 001D2090(ch, 0x23D4D0),
 * 001D1F80(ch, 1, 1), 001D6F60(ch, 0x2004818599422020, 0x80),
 * 001D1FF0(ch, 1); then on the channel's DMA row (D_00275670 + 4 ch, its
 * +0x10 the write pointer) three reference tags of 16 bytes (byte +3 =
 * 0x30, word +4 = the address, halfword +0 = the quadword count): the
 * context's 0x817140 + (D_00275670 +0x9C << 7) row (8), then 0x2514B0 (2)
 * when 001D2910(0) returns 0, then the model's packet model +0x40 (the
 * count model +4).
 * ---------------------------------------------------------------------- */
static void l13_ref_tag(L13 *o, uint32_t row, uint32_t addr, uint32_t count)
{
    l13_w8(o, l13_u32(o, row + 0x10) + 3, 0x30);
    l13_w32(o, l13_u32(o, row + 0x10) + 4, addr);
    l13_w16(o, l13_u32(o, row + 0x10), count);
    l13_w32(o, row + 0x10, l13_u32(o, row + 0x10) + 0x10u);
}

void l13_001D42E0(L13 *o, int32_t ch, uint32_t model)
{
    if (l13_c_001D2090(o, ch, 0x0023D4D0u)) return;
    if (l13_c_001D1F80(o, ch, 1, 1)) return;
    if (l13_c_001D6F60(o, ch, UINT64_C(0x2004818599422020), 0x80)) return;
    if (l13_c_001D1FF0(o, ch, 1)) return;
    uint32_t ctx = l13_u32(o, 0x00275670u);
    uint32_t off = l13_u32(o, ctx + 0x9C) << 7;
    l13_ref_tag(o, ctx + (uint32_t)ch * 4u, 0x00817140u + off, 8);
    int32_t r = 0;
    if (l13_c_001D2910(o, 0, &r)) return;
    if (r == 0) {
        ctx = l13_u32(o, 0x00275670u);
        l13_ref_tag(o, ctx + (uint32_t)ch * 4u, 0x002514B0u, 2);
    }
    ctx = l13_u32(o, 0x00275670u);
    uint32_t count = l13_u32(o, model + 4);
    l13_ref_tag(o, ctx + (uint32_t)ch * 4u, model + 0x40, count);
}

int em_level13_port_001D42E0(const H13 *h, int32_t ch, uint32_t model, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_001D42E0(&o, ch, model);
    return l13_end(&o);
}

/* 001D4430 (model, a1): 001D42E0(3, model); a1 goes unused. */
void l13_001D4430(L13 *o, uint32_t model, uint32_t a1)
{
    (void)a1;
    l13_001D42E0(o, 3, model);
}

int em_level13_port_001D4430(const H13 *h, uint32_t model, uint32_t a1, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_001D4430(&o, model, a1);
    return l13_end(&o);
}

/* ------------------------------------------------------------------------
 * 001CB140 (obj, model): p = the context's (D_00275670) +0x1C write
 * pointer, read first; 001D8C20(5); 001C7420(obj, 0x3F5, 3);
 * 001D4430(model, obj +0x80); an end tag at +0x1C (byte +3 = 0x60, word +4
 * = 0, halfword +0 = 0; +0x1C += 0x10); 001CAAC0(D_00275B44 + 0xB0, p);
 * 001D8C20(0).
 * ---------------------------------------------------------------------- */
void l13_001CB140(L13 *o, uint32_t obj, uint32_t model)
{
    uint32_t p = l13_u32(o, l13_u32(o, 0x00275670u) + 0x1C);
    if (l13_c_001D8C20(o, 5)) return;
    if (l13_c_001C7420(o, obj, 0x3F5, 3)) return;
    l13_001D4430(o, model, obj + 0x80);
    if (l13_failed(o)) return;
    uint32_t ctx = l13_u32(o, 0x00275670u);
    l13_w8(o, l13_u32(o, ctx + 0x1C) + 3, 0x60);
    l13_w32(o, l13_u32(o, ctx + 0x1C) + 4, 0);
    l13_w16(o, l13_u32(o, ctx + 0x1C), 0);
    l13_w32(o, ctx + 0x1C, l13_u32(o, ctx + 0x1C) + 0x10u);
    if (l13_c_001CAAC0(o, l13_u32(o, 0x00275B44u) + 0xB0, p)) return;
    l13_c_001D8C20(o, 0);
}

int em_level13_port_001CB140(const H13 *h, uint32_t a0, uint32_t a1, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_001CB140(&o, a0, a1);
    return l13_end(&o);
}

/* 001CB1F0 (obj): 001CB140(obj, obj +0x44) (a draw method of 0x26E310). */
int em_level13_port_001CB1F0(const H13 *h, uint32_t obj, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_001CB140(&o, obj, l13_u32(&o, obj + 0x44));
    return l13_end(&o);
}

/* 001F6AC0 (rec) -> 1 when the word rec +0x24 is not -1. */
int em_level13_port_001F6AC0(const H13 *h, uint32_t rec, int32_t *result, F13 *fault)
{
    L13 o;
    if (!result || l13_begin(&o, h, fault)) return -1;
    uint32_t w = l13_u32(&o, rec + 0x24);
    if (l13_failed(&o)) return -1;
    *result = ~w != 0;
    return 0;
}

/* 001F6B90 (): 001F6640(0x25D2C0). */
void l13_001F6B90(L13 *o)
{
    l13_c_001F6640(o, 0x0025D2C0u);
}

int em_level13_port_001F6B90(const H13 *h, F13 *fault)
{
    L13 o;
    if (l13_begin(&o, h, fault)) return -1;
    l13_001F6B90(&o);
    return l13_end(&o);
}
