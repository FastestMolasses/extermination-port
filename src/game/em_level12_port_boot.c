/* The twelfth level: the boot functions outside the creature family
 * (docs/LEVEL12_PORT.md section 2): the 0x141D20 actor's behaviour 5
 * (001437E0) and its side probe (00146740 / 001B39F0), the column scan
 * 001B2F70, the pair probe 001B3390, 001B1270, the float arc cosine
 * kernel and wrapper (0011BCF8 / 0011E420), the player's attribute-0x39
 * ledge state (0016EF50, 00179560, 0017F9E0, 001821E0), 001831F0,
 * 001A96F0, the matrix scale 001C6910 / 001C9570, the render-list
 * helpers 001CAFA0 / 001CB060 / 001D3F60 / 001D40D0 and 001E8B40.
 * Ground truth: the original instructions; where the decomp's text
 * differs the original wins (docs/LEVEL12_PORT.md section 0). */
#include "em_level12_port_internal.h"

#define SP_38A0 0x700038A0u
#define SP_38B0 0x700038B0u
#define SP_31B0 0x700031B0u
#define SP_3600 0x70003600u
#define SP_3A20 0x70003A20u

#define F_HALF 0x3F000000u
#define F_TWO 0x40000000u
#define F_300 0x43960000u
#define F_PI_2 0x3FC90FDBu

/* ------------------------------------------------------------------------
 * 0011BCF8 (x): the float arc cosine kernel. |x| == 1: 0 for x > 0 (as a
 * word), else pi; |x| > 1 (any word above 0x3F800000, NaNs included):
 * (x - x) / (x - x); |x| < 0.5: pi/2 for |x| <= 2**-57 (the word
 * 0x23000000), else pio2_hi - (x - (pio2_lo - x r)) with r = p(z) / q(z),
 * z = x x; x <= -0.5: pi - 2 (s + (r s - pio2_lo)), z = (1 + x) / 2, s =
 * 0011CB90(z); x >= 0.5: 2 (df + (r s + c)), z = (1 - x) / 2, s =
 * 0011CB90(z), df = s with its low 12 bits cleared, c = (z - df df) / (s +
 * df). Every operation is the EE's (em_ee_float.h) in the original order.
 * ---------------------------------------------------------------------- */
static void l12_acos_pq(uint32_t z, uint32_t *p, uint32_t *q)
{
    uint32_t f = L12_MUL(z, 0x3811EF08u);
    f = L12_ADD(f, 0x3A4F7F04u);
    f = L12_MUL(z, f);
    f = L12_ADD(f, 0xBD241146u);
    f = L12_MUL(z, f);
    f = L12_ADD(f, 0x3E4E0AA8u);
    f = L12_MUL(z, f);
    f = L12_ADD(f, 0xBEA6B090u);
    f = L12_MUL(z, f);
    f = L12_ADD(f, 0x3E2AAAABu);
    *p = L12_MUL(z, f);
    uint32_t g = L12_MUL(z, 0x3D9DC62Eu);
    g = L12_ADD(g, 0xBF303361u);
    g = L12_MUL(z, g);
    g = L12_ADD(g, 0x4001572Du);
    g = L12_MUL(z, g);
    g = L12_ADD(g, 0xC019D139u);
    g = L12_MUL(z, g);
    *q = L12_ADD(g, F_ONE);
}

uint32_t l12_0011BCF8(L12 *o, uint32_t x)
{
    uint32_t ix = x & 0x7FFFFFFFu;
    uint32_t p, q, s;
    if (ix == 0x3F800000u) return (int32_t)x > 0 ? 0 : 0x40490FDBu;
    if ((int32_t)ix > 0x3F800000) {
        uint32_t d = L12_SUB(x, x);
        return L12_DIV(d, d);
    }
    if (!((int32_t)ix > 0x3EFFFFFF)) {
        if (!((int32_t)ix > 0x23000000)) return F_PI_2;
        uint32_t z = L12_MUL(x, x);
        l12_acos_pq(z, &p, &q);
        uint32_t r = L12_DIV(p, q);
        uint32_t f = L12_SUB(0x33A22168u, L12_MUL(x, r));
        f = L12_SUB(x, f);
        return L12_SUB(0x3FC90FDAu, f);
    }
    if ((int32_t)x < 0) {
        uint32_t z = L12_MUL(L12_ADD(x, F_ONE), F_HALF);
        l12_acos_pq(z, &p, &q);
        if (l12_c_0011CB90(o, z, &s)) return 0;
        uint32_t r = L12_DIV(p, q);
        uint32_t f = L12_SUB(L12_MUL(r, s), 0x33A22168u);
        f = L12_ADD(s, f);
        f = L12_ADD(f, f);
        return L12_SUB(0x40490FDAu, f);
    }
    {
        uint32_t z = L12_MUL(L12_SUB(F_ONE, x), F_HALF);
        if (l12_c_0011CB90(o, z, &s)) return 0;
        uint32_t df = s & 0xFFFFF000u;
        uint32_t c = L12_DIV(L12_SUB(z, L12_MUL(df, df)), L12_ADD(s, df));
        l12_acos_pq(z, &p, &q);
        uint32_t r = L12_DIV(p, q);
        uint32_t f = L12_ADD(L12_MUL(r, s), c);
        f = L12_ADD(df, f);
        return L12_ADD(f, f);
    }
}

int em_level12_port_0011BCF8(const EmLevel12PortHooks *h, float x, float *result, EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    uint32_t v = l12_0011BCF8(&o, l12_bits(x));
    if (l12_failed(&o)) return -1;
    *result = l12_float(v);
    return 0;
}

/* ------------------------------------------------------------------------
 * 0011E420 (x, sp): the arc cosine wrapper. v = 0011BCF8(x); with the
 * error mode 0x26C5D0 not -1, 0011E080(x) zero (not a NaN) and 1 <
 * 0011DF78(x): the exception record at sp - 0x60 (type 1, the name
 * 0x26C630, the argument 00128350(x) twice, the result 0 at +0x18, the
 * errno word +0x20 = 0); without mode 2, 0011DB90(record); mode 2 or a
 * zero result: *0011FD78() = 0x21 (EDOM); a nonzero +0x20: *0011FD78() =
 * +0x20; the result is then 00127758(+0x18), not v.
 * ---------------------------------------------------------------------- */
uint32_t l12_0011E420(L12 *o, uint32_t x, uint32_t sp)
{
    uint32_t v = l12_0011BCF8(o, x);
    if (l12_failed(o)) return 0;
    int32_t mode = l12_s32(o, 0x26C5D0u);
    if (mode == -1) return v;
    int32_t n;
    if (l12_c_0011E080(o, x, &n)) return 0;
    if (n != 0) return v;
    uint32_t a;
    if (l12_c_0011DF78(o, x, &a)) return 0;
    if (!L12_LT(F_ONE, a)) return v;
    uint32_t fr = sp - 0x60u;
    uint64_t dq;
    l12_w32(o, fr + 0, 1);
    l12_w32(o, fr + 4, 0x26C630u);
    l12_w32(o, fr + 0x20, 0);
    if (l12_c_00128350(o, x, &dq)) return 0;
    l12_w64(o, fr + 8, dq);
    l12_w64(o, fr + 0x18, 0);
    l12_w64(o, fr + 0x10, dq);
    int edom = 1;
    uint32_t err = 0;
    if (mode != 2) {
        int32_t m;
        if (l12_c_0011DB90(o, fr, &m)) return 0;
        err = l12_u32(o, fr + 0x20);
        edom = m == 0;
    }
    if (edom) {
        uint32_t p;
        if (l12_c_0011FD78(o, &p)) return 0;
        l12_w32(o, p, 0x21);
        err = l12_u32(o, fr + 0x20);
    }
    if (err != 0) {
        uint32_t p;
        if (l12_c_0011FD78(o, &p)) return 0;
        l12_w32(o, p, l12_u32(o, fr + 0x20));
    }
    uint32_t r;
    if (l12_c_00127758(o, l12_u64(o, fr + 0x18), &r)) return 0;
    return r;
}

int em_level12_port_0011E420(const EmLevel12PortHooks *h, float x, uint32_t sp, float *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    uint32_t v = l12_0011E420(&o, l12_bits(x), sp);
    if (l12_failed(&o)) return -1;
    *result = l12_float(v);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B1270 (obj, px, py): 001B1470(0011E620(-1 * (px - obj[1]), py -
 * obj[2])).
 * ---------------------------------------------------------------------- */
uint32_t l12_001B1270(L12 *o, uint32_t obj, uint32_t px, uint32_t py)
{
    uint32_t y = l12_u32(o, obj + 4);
    uint32_t z = l12_u32(o, obj + 8);
    uint32_t a, r;
    if (l12_c_0011E620(o, L12_MUL(0xBF800000u, L12_SUB(px, y)), L12_SUB(py, z), &a)) return 0;
    if (l12_c_001B1470(o, a, &r)) return 0;
    return r;
}

int em_level12_port_001B1270(const EmLevel12PortHooks *h, uint32_t obj, float px, float py, float *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    uint32_t v = l12_001B1270(&o, obj, l12_bits(px), l12_bits(py));
    if (l12_failed(&o)) return -1;
    *result = l12_float(v);
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B2F70 (pos, out): 0019BC40(pos); over the column count 0x700031E0
 * (re-read each step): a flagged entry (0x70003170) whose slope
 * (0011DF78 of 0x282250[i]) <= pi/3: the first sets *out = height
 * (0x700030F0[i]) and found; a later one higher than *out returns 1 when
 * pos +4 < it, else becomes *out. An unflagged entry after one was found
 * returns 1 when pos +4 <= its height. Returns found.
 * ---------------------------------------------------------------------- */
int32_t l12_001B2F70(L12 *o, uint32_t pos, uint32_t out)
{
    uint32_t g;
    if (l12_c_0019BC40(o, pos)) return 0;
    if (l12_u32(o, 0x700031E0u) == 0) return 0;
    int32_t found = 0;
    for (int32_t i = 0; i < l12_s32(o, 0x700031E0u); i++) {
        uint32_t hp = 0x700030F0u + 4u * (uint32_t)i;
        if (l12_u16(o, 0x70003170u + 2u * (uint32_t)i) & 1u) {
            if (l12_c_0011DF78(o, l12_u32(o, 0x282250u + 4u * (uint32_t)i), &g)) return 0;
            if (!L12_LE(g, 0x3F860A92u)) continue;
            if (found == 0) {
                found = 1;
                l12_w32(o, out, l12_u32(o, hp));
                continue;
            }
            uint32_t cur = l12_u32(o, out);
            uint32_t h = l12_u32(o, hp);
            if (!L12_LT(cur, h)) continue;
            if (L12_LT(l12_u32(o, pos + 4), h)) return 1;
            l12_w32(o, out, h);
        } else if (found != 0) {
            uint32_t y = l12_u32(o, pos + 4);
            if (L12_LE(y, l12_u32(o, hp))) return 1;
        }
    }
    return found;
}

int em_level12_port_001B2F70(const EmLevel12PortHooks *h, uint32_t pos, uint32_t out, int32_t *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_001B2F70(&o, pos, out);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001B3390 (a0, a1, a2, f12, sp): a2 +0xC = a1 +0xC = 1.0; r =
 * 0019B2C0(a1, a2, 6); 0 -> 0; 0019A310(sp - 4) zero -> 0; the value
 * there <= f12 -> 0; else 001028B8(a0 +0xB0, a0 +0xB0, 0x700031C0) and r.
 * ---------------------------------------------------------------------- */
int32_t l12_001B3390(L12 *o, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t f12, uint32_t sp)
{
    int32_t r, q;
    l12_w32(o, a2 + 0xC, F_ONE);
    l12_w32(o, a1 + 0xC, F_ONE);
    if (l12_c_0019B2C0(o, a1, a2, 6, &r)) return 0;
    if (r == 0) return 0;
    if (l12_c_0019A310(o, sp - 4u, &q)) return 0;
    if (q == 0) return 0;
    if (L12_LE(l12_u32(o, sp - 4u), f12)) return 0;
    if (l12_c_001028B8(o, a0 + 0xB0, a0 + 0xB0, 0x700031C0u)) return 0;
    return r;
}

int em_level12_port_001B3390(const EmLevel12PortHooks *h, uint32_t a0, uint32_t a1, uint32_t a2, float f12,
                             uint32_t sp, int32_t *result, EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_001B3390(&o, a0, a1, a2, l12_bits(f12), sp);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* One side of 001B39F0: the point (dx, 0, 0, 1) at 0x70003600 through
 * 001B2B10(self), shifted by seg (001028B8); 0019A6F0(self, seg, it, 7,
 * 0x40) set -> 001028D0(it, 0x700031B0, seg) and *dst = 0011E748(x x + y
 * y + z z) (ADDA, then MADD). */
static void l12_side(L12 *o, uint32_t self, uint32_t seg, uint32_t dx, uint32_t dst)
{
    int32_t r;
    l12_w32(o, SP_3600, dx);
    l12_w32(o, SP_3600 + 4, 0);
    l12_w32(o, SP_3600 + 8, 0);
    l12_w32(o, SP_3600 + 12, F_ONE);
    if (l12_c_001B2B10(o, self, SP_3600, SP_3600)) return;
    if (l12_c_001028B8(o, SP_3600, SP_3600, seg)) return;
    if (l12_c_0019A6F0(o, self, seg, SP_3600, 7, 0x40, &r)) return;
    if (r == 0) return;
    if (l12_c_001028D0(o, SP_3600, SP_31B0, seg)) return;
    uint32_t x = l12_u32(o, SP_3600);
    uint32_t y = l12_u32(o, SP_3600 + 4);
    uint32_t xx = L12_MUL(x, x);
    uint32_t yy = L12_MUL(y, y);
    uint32_t z = l12_u32(o, SP_3600 + 8);
    uint32_t sum = em_ee_madd_bits(em_ee_adda_bits(xx, yy), z, z);
    uint32_t d;
    if (l12_c_0011E748(o, sum, &d)) return;
    l12_w32(o, dst, d);
}

/* ------------------------------------------------------------------------
 * 001B39F0 (self, seg, out): out[1] = out[0] = 500; the side +200 into
 * out[0] and -200 into out[1]; |out[0] - out[1]| <= 1.5 -> (00122BB8() >>
 * 11) & 1, else out[0] < out[1] ? 0 : 1.
 * ---------------------------------------------------------------------- */
int32_t l12_001B39F0(L12 *o, uint32_t self, uint32_t seg, uint32_t out)
{
    uint32_t g;
    l12_w32(o, out + 4, 0x43FA0000u);
    l12_w32(o, out + 0, 0x43FA0000u);
    l12_side(o, self, seg, 0x43480000u, out + 0);
    if (l12_failed(o)) return 0;
    l12_side(o, self, seg, 0xC3480000u, out + 4);
    if (l12_failed(o)) return 0;
    {
        uint32_t a = l12_u32(o, out + 0);
        uint32_t b = l12_u32(o, out + 4);
        if (l12_c_0011DF78(o, L12_SUB(a, b), &g)) return 0;
    }
    if (L12_LE(g, 0x3FC00000u)) {
        int32_t r;
        if (l12_c_00122BB8(o, &r)) return 0;
        return l12_sra((uint32_t)r, 11) & 1;
    }
    {
        uint32_t a = l12_u32(o, out + 0);
        uint32_t b = l12_u32(o, out + 4);
        return L12_LT(a, b) ? 0 : 1;
    }
}

int em_level12_port_001B39F0(const EmLevel12PortHooks *h, uint32_t self, uint32_t seg, uint32_t out,
                             int32_t *result, EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_001B39F0(&o, self, seg, out);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00146740 (e, d, sp): the side probe toward the hit object. 0x700038A0 =
 * e +0xB0, 0x700038B0 = 0x700031B0 (00102948), both y = 0; the distance
 * (001B15D0) to 0x70003A20. Within 23.5: the forward (0, 0, 1) through
 * 001B2B10(e), the object's (x, 0, z, 1) at 0x700038B0 normalized
 * (00102760); 0011E420(00102738(the two)) above 2.3561945 -> 3, else
 * shifted by e +0xB0 and d +0x79 = 001B1380(it, e +0xB0, e +0xC4) -> 1.
 * Beyond: (0, 0, distance / 2) through 001B2B10(e), shifted (001028B8
 * with e +0xB0), its y + 3; d +0x79 = 001B39F0(e, it, 0x700038B0); the
 * word 0x700038B0 + (1 - d +0x79) * 4 below 10 -> 3, else 1.
 * ---------------------------------------------------------------------- */
int32_t l12_00146740(L12 *o, uint32_t e, uint32_t d, uint32_t sp)
{
    uint32_t dist;
    if (l12_c_00102948(o, SP_38A0, e + 0xB0)) return 0;
    if (l12_c_00102948(o, SP_38B0, SP_31B0)) return 0;
    l12_w32(o, SP_38B0 + 4, 0);
    l12_w32(o, SP_38A0 + 4, 0);
    if (l12_c_001B15D0(o, SP_38A0, SP_38B0, &dist)) return 0;
    l12_w32(o, SP_3A20, dist);
    if (L12_LE(dist, 0x41BC0000u)) {
        uint32_t dot, ang;
        int32_t t;
        l12_w32(o, SP_38A0, 0);
        l12_w32(o, SP_38A0 + 4, 0);
        l12_w32(o, SP_38A0 + 8, F_ONE);
        l12_w32(o, SP_38A0 + 12, F_ONE);
        if (l12_c_001B2B10(o, e, SP_38A0, SP_38A0)) return 0;
        {
            uint32_t hit = l12_u32(o, 0x700031D0u);
            l12_w32(o, SP_38B0, l12_u32(o, hit + 0x24));
            l12_w32(o, SP_38B0 + 4, 0);
            l12_w32(o, SP_38B0 + 8, l12_u32(o, hit + 0x2C));
            l12_w32(o, SP_38B0 + 12, F_ONE);
        }
        if (l12_c_00102760(o, SP_38B0, SP_38B0)) return 0;
        if (l12_c_00102738(o, SP_38A0, SP_38B0, &dot)) return 0;
        ang = l12_0011E420(o, dot, sp - 0x30u);
        if (l12_failed(o)) return 0;
        if (!L12_LE(ang, 0x4016CBE4u)) return 3;
        if (l12_c_001028B8(o, SP_38B0, SP_38B0, e + 0xB0)) return 0;
        if (l12_c_001B1380(o, SP_38B0, e + 0xB0, l12_u32(o, e + 0xC4), &t)) return 0;
        l12_w8(o, d + 0x79, (uint32_t)t);
        return 1;
    }
    {
        uint32_t half = L12_DIV(dist, F_TWO);
        l12_w32(o, SP_38A0, 0);
        l12_w32(o, SP_38A0 + 4, 0);
        l12_w32(o, SP_38A0 + 8, half);
        l12_w32(o, SP_38A0 + 12, F_ONE);
    }
    if (l12_c_001B2B10(o, e, SP_38A0, SP_38A0)) return 0;
    if (l12_c_001028B8(o, SP_38A0, e + 0xB0, SP_38A0)) return 0;
    l12_w32(o, SP_38A0 + 4, L12_ADD(l12_u32(o, SP_38A0 + 4), 0x40400000u));
    {
        int32_t t = l12_001B39F0(o, e, SP_38A0, SP_38B0);
        if (l12_failed(o)) return 0;
        l12_w8(o, d + 0x79, (uint32_t)t);
    }
    {
        int32_t side = l12_s8(o, d + 0x79);
        uint32_t f = l12_u32(o, SP_38B0 + (uint32_t)(1 - side) * 4u);
        return L12_LT(f, 0x41200000u) ? 3 : 1;
    }
}

int em_level12_port_00146740(const EmLevel12PortHooks *h, uint32_t e, uint32_t d, uint32_t sp, int32_t *result,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_00146740(&o, e, d, sp);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 001437E0 (e, d): the 0x141D20 actor's behaviour 5, by e +6.
 *   0: e +6 = 1, d +0x20 = 0, e +0x8C = 1.0, d +0x54 = 0.08, d +0x4C =
 *      +0x50 = 0, d +0x72 = 0xFF; d +0x78 set: 001EFE00(0x8000004F, e),
 *      001C67E0(e, 0x25, 10, 132); else 001EFE00(0x8000004E, e),
 *      001C67E0(e, 0x25, 10, 0).
 *   1: d +0x72 = 0xFF; with e +0x2C bit 15 clear, e +0x3C <= 110 and d
 *      +0x54 nonzero: e +0x8C += d +0x54; a rate > 0 with +0x8C >= 4:
 *      +0x8C = 4, rate -0.08, the model 0x28A490[0x7E or 0x7D by bit 7 of
 *      e +0xD] (001CA5E0 with 0, scale 1, d +0x78 = 0, cue 0x83D; or with
 *      8, scale (0.859375, 0.88, 0.88), +0x8C = 3.5, d +0x78 = 1, cue
 *      0x83C; the cue 001FBD50(e, cue, 0, 300) only while d +0x20 is 0,
 *      which it sets to 1); a rate <= 0 with +0x8C <= 1: +0x8C = 1, rate
 *      0. Then with bit 0x1000 of d +0x30 and the rate 0: e +5 = +6 = 0,
 *      d +0x72 = 0, d +0x60 = (00122BB8() >> 17) % 900 + 300.
 * ---------------------------------------------------------------------- */
void l12_001437E0(L12 *o, uint32_t e, uint32_t d)
{
    uint32_t st = l12_u8(o, e + 6);
    if (st == 0) {
        l12_w8(o, e + 6, st + 1u);
        l12_w32(o, d + 0x20, 0);
        l12_w32(o, e + 0x8C, F_ONE);
        l12_w32(o, d + 0x54, 0x3DA3D70Au);
        l12_w32(o, d + 0x4C, 0);
        l12_w32(o, d + 0x50, 0);
        l12_w8(o, d + 0x72, 0xFF);
        if (l12_s8(o, d + 0x78) != 0) {
            if (l12_c_001EFE00(o, (int32_t)0x8000004Fu, e)) return;
            l12_c_001C67E0(o, e, 0x25, 0x41200000u, 0x43040000u);
        } else {
            if (l12_c_001EFE00(o, (int32_t)0x8000004Eu, e)) return;
            l12_c_001C67E0(o, e, 0x25, 0x41200000u, 0);
        }
        return;
    }
    if (st != 1) return;
    l12_w8(o, d + 0x72, 0xFF);
    if (!((uint32_t)l12_s16(o, e + 0x2C) & 0x8000u) && L12_LE(l12_u32(o, e + 0x3C), 0x42DC0000u)) {
        uint32_t rate = l12_u32(o, d + 0x54);
        if (!L12_EQ(0, rate)) {
            l12_w32(o, e + 0x8C, L12_ADD(l12_u32(o, e + 0x8C), rate));
            if (!L12_LE(l12_u32(o, d + 0x54), 0)) {
                if (!L12_LT(l12_u32(o, e + 0x8C), 0x40800000u)) {
                    l12_w32(o, e + 0x8C, 0x40800000u);
                    l12_w32(o, d + 0x54, 0xBDA3D70Au);
                    uint32_t dd = l12_u8(o, e + 0xD);
                    uint32_t idx = (dd & 0x80u) ? 0x7Eu : 0x7Du;
                    if (l12_s8(o, d + 0x78) != 0) {
                        l12_w8(o, d + 0x78, 0);
                        if (l12_c_001CA5E0(o, e, l12_u32(o, 0x28A490u + idx * 4u), 0)) return;
                        l12_w32(o, e + 0x80, F_ONE);
                        l12_w32(o, e + 0x84, F_ONE);
                        l12_w32(o, e + 0x88, F_ONE);
                        if (l12_u32(o, d + 0x20) == 0) {
                            l12_w32(o, d + 0x20, 1);
                            if (l12_c_001FBD50(o, e, 0x83D, 0, F_300)) return;
                        }
                    } else {
                        l12_w8(o, d + 0x78, 1);
                        if (l12_c_001CA5E0(o, e, l12_u32(o, 0x28A490u + idx * 4u), 8)) return;
                        l12_w32(o, e + 0x80, 0x3F5C0000u);
                        l12_w32(o, e + 0x84, 0x3F616666u);
                        l12_w32(o, e + 0x88, 0x3F616666u);
                        l12_w32(o, e + 0x8C, 0x40600000u);
                        if (l12_u32(o, d + 0x20) == 0) {
                            l12_w32(o, d + 0x20, 1);
                            if (l12_c_001FBD50(o, e, 0x83C, 0, F_300)) return;
                        }
                    }
                }
            } else if (L12_LE(l12_u32(o, e + 0x8C), F_ONE)) {
                l12_w32(o, e + 0x8C, F_ONE);
                l12_w32(o, d + 0x54, 0);
            }
        }
    }
    if (!(l12_u32(o, d + 0x30) & 0x1000u)) return;
    if (!L12_EQ(0, l12_u32(o, d + 0x54))) return;
    l12_w8(o, e + 5, 0);
    l12_w8(o, e + 6, 0);
    l12_w8(o, d + 0x72, 0);
    {
        int32_t r;
        if (l12_c_00122BB8(o, &r)) return;
        l12_w16(o, d + 0x60, (uint32_t)(l12_sra((uint32_t)r, 17) % 900 + 300));
    }
}

int em_level12_port_001437E0(const EmLevel12PortHooks *h, uint32_t e, uint32_t d, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001437E0(&o, e, d);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001821E0 (a): with a +0x224 or a +0x22C nonzero or bit 1 of a +0xF: a +4
 * = 2, +5 = 9, +6 = 0, returns 1; else 0.
 * ---------------------------------------------------------------------- */
int32_t l12_001821E0(L12 *o, uint32_t a)
{
    if (L12_EQ(l12_u32(o, a + 0x224), 0) && L12_EQ(l12_u32(o, a + 0x22C), 0) && !(l12_u8(o, a + 0xF) & 2u))
        return 0;
    l12_w8(o, a + 4, 2);
    l12_w8(o, a + 5, 9);
    l12_w8(o, a + 6, 0);
    return 1;
}

int em_level12_port_001821E0(const EmLevel12PortHooks *h, uint32_t a, int32_t *result, EmLevel12PortFault *fault)
{
    L12 o;
    if (!result || l12_begin(&o, h, fault)) return -1;
    int32_t v = l12_001821E0(&o, a);
    if (l12_failed(&o)) return -1;
    *result = v;
    return 0;
}

/* ------------------------------------------------------------------------
 * 00179560 (p): the shuffle step. p +0x314 = 0; p +0xB0 += p +0x38 *
 * 0011DE90(p +0xC4); p +0xB8 -= p +0x38 * 0011E2A8(p +0xC4); 001C94B0(p
 * +0xD0, +0xB0, +0xC0, +0x60); the point (-4.5 for p +0x38 <= 0, else
 * 4.5; 10; 0; 1) through the matrix p +0xD0 (001026A0 to 0x700038B0);
 * 0019AD00(p, it, 0x80000006).
 * ---------------------------------------------------------------------- */
void l12_00179560(L12 *o, uint32_t p)
{
    uint32_t r;
    int32_t ignored;
    l12_w8(o, p + 0x314, 0);
    if (l12_c_0011DE90(o, l12_u32(o, p + 0xC4), &r)) return;
    {
        uint32_t k = l12_u32(o, p + 0x38);
        uint32_t x = l12_u32(o, p + 0xB0);
        l12_w32(o, p + 0xB0, L12_ADD(x, L12_MUL(k, r)));
    }
    if (l12_c_0011E2A8(o, l12_u32(o, p + 0xC4), &r)) return;
    {
        uint32_t k = l12_u32(o, p + 0x38);
        uint32_t z = l12_u32(o, p + 0xB8);
        l12_w32(o, p + 0xB8, L12_SUB(z, L12_MUL(k, r)));
    }
    if (l12_c_001C94B0(o, p + 0xD0, p + 0xB0, p + 0xC0, p + 0x60)) return;
    l12_w32(o, SP_38A0, L12_LE(l12_u32(o, p + 0x38), 0) ? 0xC0900000u : 0x40900000u);
    l12_w32(o, SP_38A0 + 4, 0x41200000u);
    l12_w32(o, SP_38A0 + 8, 0);
    l12_w32(o, SP_38A0 + 12, F_ONE);
    if (l12_c_001026A0(o, SP_38B0, p + 0xD0, SP_38A0)) return;
    l12_c_0019AD00(o, p, SP_38B0, (int32_t)0x80000006u, &ignored);
}

int em_level12_port_00179560(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_00179560(&o, p);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 0017F9E0 (p): the attribute-0x39 ledge entry. With the record at
 * 0x700031D0: 0x70003A20 = 0011E620(-rec +0x3C, rec +0x34), then
 * 001B1470(pi/2 + it) into 0x70003A20 and p +0x218; 0x70003A24 =
 * 001B1470(0x70003A20 - p +0xC4). Not negative: p +0xC4 = 001B1470(p
 * +0x218 - pi/2), p +0x2F1 = 0, 0019F680(0x700038A0, rec, 2) and
 * (0x700038B0, rec, 3); negative: p +0xC4 = 001B1470(pi/2 + p +0x218),
 * +0x2F1 = 1, the corners 0 / 1. p +0xB0 / +0xB8 = the corners' mean x /
 * z; p +5 = 0x1B, +6 = 0, +0x1F0 = 0x2F, +0x1F1 = 0; 00174A50(p, 0).
 * ---------------------------------------------------------------------- */
void l12_0017F9E0(L12 *o, uint32_t p)
{
    uint32_t a, t;
    uint32_t rec = l12_u32(o, 0x700031D0u);
    {
        uint32_t f3c = l12_u32(o, rec + 0x3C);
        uint32_t f34 = l12_u32(o, rec + 0x34);
        if (l12_c_0011E620(o, L12_NEG(f3c), f34, &a)) return;
    }
    l12_w32(o, SP_3A20, a);
    if (l12_c_001B1470(o, L12_ADD(F_PI_2, l12_u32(o, SP_3A20)), &t)) return;
    l12_w32(o, SP_3A20, t);
    l12_w32(o, p + 0x218, t);
    {
        uint32_t c4 = l12_u32(o, p + 0xC4);
        uint32_t w = l12_u32(o, SP_3A20);
        if (l12_c_001B1470(o, L12_SUB(w, c4), &t)) return;
    }
    l12_w32(o, SP_3A20 + 4, t);
    if (!L12_LT(t, 0)) {
        if (l12_c_001B1470(o, L12_SUB(l12_u32(o, p + 0x218), F_PI_2), &t)) return;
        l12_w32(o, p + 0xC4, t);
        l12_w8(o, p + 0x2F1, 0);
        if (l12_c_0019F680(o, SP_38A0, l12_u32(o, 0x700031D0u), 2)) return;
        if (l12_c_0019F680(o, SP_38B0, l12_u32(o, 0x700031D0u), 3)) return;
    } else {
        if (l12_c_001B1470(o, L12_ADD(F_PI_2, l12_u32(o, p + 0x218)), &t)) return;
        l12_w32(o, p + 0xC4, t);
        l12_w8(o, p + 0x2F1, 1);
        if (l12_c_0019F680(o, SP_38A0, l12_u32(o, 0x700031D0u), 0)) return;
        if (l12_c_0019F680(o, SP_38B0, l12_u32(o, 0x700031D0u), 1)) return;
    }
    {
        uint32_t x0 = l12_u32(o, SP_38A0);
        uint32_t x1 = l12_u32(o, SP_38B0);
        l12_w32(o, p + 0xB0, L12_DIV(L12_ADD(x0, x1), F_TWO));
    }
    {
        uint32_t z0 = l12_u32(o, SP_38A0 + 8);
        uint32_t z1 = l12_u32(o, SP_38B0 + 8);
        l12_w32(o, p + 0xB8, L12_DIV(L12_ADD(z0, z1), F_TWO));
    }
    l12_w8(o, p + 5, 0x1B);
    l12_w8(o, p + 6, 0);
    l12_w8(o, p + 0x1F0, 0x2F);
    l12_w8(o, p + 0x1F1, 0);
    l12_c_00174A50(o, p, 0);
}

int em_level12_port_0017F9E0(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0017F9E0(&o, p);
    return l12_end(&o);
}

/* p +0xB0 += p +0x2E0, p +0xB8 += p +0x2E8 (each step's operand read
 * first). */
static void l12_drift(L12 *o, uint32_t p)
{
    uint32_t dx = l12_u32(o, p + 0x2E0);
    uint32_t x = l12_u32(o, p + 0xB0);
    l12_w32(o, p + 0xB0, L12_ADD(x, dx));
    uint32_t dz = l12_u32(o, p + 0x2E8);
    uint32_t z = l12_u32(o, p + 0xB8);
    l12_w32(o, p + 0xB8, L12_ADD(z, dz));
}

/* ------------------------------------------------------------------------
 * 0016EF50 (p): the ledge state machine, by p +6 (docs/LEVEL12_PORT.md
 * section 2):
 *   0: +6 = 1, the clip 0x145 / 0x146 by p +0x2F1 (001749A0 with 8), the
 *      drift 0x3E06BCA2 * (cos, sin)(p +0xC4) (0011E2A8 / 0011DE90) into
 *      +0x2E0 / +0x2E8 via 0x70003A20;
 *   1: bit 0x1000 of +0x200: +6 = 0xA, +7 = 0, +0x1F1 = 1, +0xC4 =
 *      +0x218, 001749A0(p, 00188610(p), 0, 0); else the drift;
 *   0xA (+6 = 0xB, +0x38 = 0) and 0xB: 001821E0(p) set returns at once;
 *      +7 0: 001751A0(p), +0x25C = +0x23F, by the word +0x24C: 0 -> the
 *      point (0, 0, 5) through +0xD0 into +0xB0 (001026A0), +0xB4 -= 0.2,
 *      +0x25C = 1, +5 = 5, +6 = 0, +0x1F0 = 0xB; 3 / 2 -> the clip
 *      0x14B / 0x149 (0x14A / 0x148) by +0x23F == 3, +0x26C = +0x204 =
 *      0x248670[+0x23F] (read once), +0x2F1 = 1 (0), +7 += 1; any value
 *      -> +0x38 = +0x21C = 0; +7 1: bit 0x1000 of +0x200 -> +0x23B 0x39:
 *      +7 = 0, 001749A0(p, 00188610(p), 0, 12), else +6 = 0x14, +0x1F1 =
 *      2; without it: +0x204 = +0x26C, +0x38 = **0x275B40 - +0x21C,
 *      +0x21C = **0x275B40, 00179560(p);
 *   0x14: +6 = 0x15, 0x70003A20 = 0x3E06BCA2, by +0x2F1 the clip 0x14C
 *      (+0x218 = 001B1470(+0xC4 - pi/2), +0x2E0 = -k sin, +0x2E8 = k cos)
 *      or 0x14D (+0x218 = 001B1470(pi/2 + +0xC4), +0x2E0 = k sin, +0x2E8 =
 *      k * -cos);
 *   0x15: bit 0x8000 of +0x200 clear: +6 = 0x16, 001764E0 with +0xC4 =
 *      +0x218 (kept in +0x26C and put back); then the drift and
 *      001764E0(p);
 *   0x16: bit 0x1000: 001C6DA0(p), +0xC4 = +0x218, 00174AB0(p), +5 = +6 =
 *      +0x1F0 = 0, 001764E0(p); else the drift and 001764E0(p).
 * Every case but the 001821E0 return ends with +0xB4 += -0.2,
 * 00175900(p, 0), 001796C0(p).
 * ---------------------------------------------------------------------- */
void l12_0016EF50(L12 *o, uint32_t p)
{
    uint32_t st = l12_u8(o, p + 6);
    uint32_t r;
    int32_t v;
    switch (st) {
    case 0x16:
        if (l12_u32(o, p + 0x200) & 0x1000u) {
            if (l12_c_001C6DA0(o, p)) return;
            l12_w32(o, p + 0xC4, l12_u32(o, p + 0x218));
            if (l12_c_00174AB0(o, p)) return;
            l12_w8(o, p + 5, 0);
            l12_w8(o, p + 6, 0);
            l12_w8(o, p + 0x1F0, 0);
        } else {
            l12_drift(o, p);
        }
        if (l12_c_001764E0(o, p)) return;
        break;
    case 0x15:
        if (!(l12_u32(o, p + 0x200) & 0x8000u)) {
            l12_w8(o, p + 6, st + 1u);
            l12_w32(o, p + 0x26C, l12_u32(o, p + 0xC4));
            l12_w32(o, p + 0xC4, l12_u32(o, p + 0x218));
            if (l12_c_001764E0(o, p)) return;
            l12_w32(o, p + 0xC4, l12_u32(o, p + 0x26C));
        }
        l12_drift(o, p);
        if (l12_c_001764E0(o, p)) return;
        break;
    case 0x14:
        l12_w8(o, p + 6, st + 1u);
        l12_w32(o, SP_3A20, 0x3E06BCA2u);
        if (l12_u8(o, p + 0x2F1) == 0) {
            if (l12_c_001749A0(o, p, 0x14C, 0, 0x41000000u)) return;
            if (l12_c_001B1470(o, L12_SUB(l12_u32(o, p + 0xC4), F_PI_2), &r)) return;
            l12_w32(o, p + 0x218, r);
            if (l12_c_0011DE90(o, l12_u32(o, p + 0xC4), &r)) return;
            l12_w32(o, p + 0x2E0, L12_MUL(L12_NEG(l12_u32(o, SP_3A20)), r));
            if (l12_c_0011E2A8(o, l12_u32(o, p + 0xC4), &r)) return;
            l12_w32(o, p + 0x2E8, L12_MUL(l12_u32(o, SP_3A20), r));
        } else {
            if (l12_c_001749A0(o, p, 0x14D, 0, 0x41000000u)) return;
            if (l12_c_001B1470(o, L12_ADD(F_PI_2, l12_u32(o, p + 0xC4)), &r)) return;
            l12_w32(o, p + 0x218, r);
            if (l12_c_0011DE90(o, l12_u32(o, p + 0xC4), &r)) return;
            l12_w32(o, p + 0x2E0, L12_MUL(l12_u32(o, SP_3A20), r));
            if (l12_c_0011E2A8(o, l12_u32(o, p + 0xC4), &r)) return;
            {
                uint32_t nc = L12_NEG(r);
                l12_w32(o, p + 0x2E8, L12_MUL(l12_u32(o, SP_3A20), nc));
            }
        }
        break;
    case 0xA:
    case 0xB: {
        if (st == 0xA) {
            l12_w8(o, p + 6, st + 1u);
            l12_w32(o, p + 0x38, 0);
        }
        int32_t gate = l12_001821E0(o, p);
        if (l12_failed(o) || gate != 0) return;
        uint32_t sub = l12_u8(o, p + 7);
        if (sub == 0) {
            if (l12_c_001751A0(o, p)) return;
            l12_w8(o, p + 0x25C, l12_u8(o, p + 0x23F));
            int32_t kind = l12_s32(o, p + 0x24C);
            if (kind == 0) {
                l12_w32(o, SP_38A0, 0);
                l12_w32(o, SP_38A0 + 4, 0);
                l12_w32(o, SP_38A0 + 8, 0x40A00000u);
                l12_w32(o, SP_38A0 + 12, F_ONE);
                if (l12_c_001026A0(o, p + 0xB0, p + 0xD0, SP_38A0)) return;
                l12_w32(o, p + 0xB4, L12_ADD(l12_u32(o, p + 0xB4), 0xBE4CCCCDu));
                l12_w8(o, p + 0x25C, 1);
                l12_w8(o, p + 5, 5);
                l12_w8(o, p + 6, 0);
                l12_w8(o, p + 0x1F0, 0xB);
            } else if (kind == 3 || kind == 2) {
                if (l12_u8(o, p + 0x23F) == 3u) {
                    if (l12_c_001749A0(o, p, kind == 3 ? 0x14B : 0x14A, 0, F_ONE)) return;
                } else {
                    if (l12_c_001749A0(o, p, kind == 3 ? 0x149 : 0x148, 0, F_ONE)) return;
                }
                uint32_t f = l12_u32(o, 0x248670u + l12_u8(o, p + 0x23F) * 4u);
                l12_w32(o, p + 0x26C, f);
                l12_w32(o, p + 0x204, f);
                l12_w8(o, p + 0x2F1, kind == 3 ? 1u : 0u);
                l12_w8(o, p + 7, l12_u8(o, p + 7) + 1u);
            }
            l12_w32(o, p + 0x38, 0);
            l12_w32(o, p + 0x21C, 0);
        } else if (sub == 1) {
            if (l12_u32(o, p + 0x200) & 0x1000u) {
                if (l12_u8(o, p + 0x23B) != 0x39u) {
                    l12_w8(o, p + 6, 0x14);
                    l12_w8(o, p + 0x1F1, 2);
                } else {
                    l12_w8(o, p + 7, 0);
                    if (l12_c_00188610(o, p, &v)) return;
                    if (l12_c_001749A0(o, p, v, 0, 0x41400000u)) return;
                }
            } else {
                l12_w32(o, p + 0x204, l12_u32(o, p + 0x26C));
                uint32_t pp = l12_u32(o, 0x275B40u);
                uint32_t t21c = l12_u32(o, p + 0x21C);
                uint32_t q = l12_u32(o, pp);
                uint32_t now = l12_u32(o, q);
                l12_w32(o, p + 0x38, L12_SUB(now, t21c));
                pp = l12_u32(o, 0x275B40u);
                q = l12_u32(o, pp);
                l12_w32(o, p + 0x21C, l12_u32(o, q));
                l12_00179560(o, p);
                if (l12_failed(o)) return;
            }
        }
        break;
    }
    case 1:
        if (l12_u32(o, p + 0x200) & 0x1000u) {
            l12_w8(o, p + 6, 0xA);
            l12_w8(o, p + 7, 0);
            l12_w8(o, p + 0x1F1, 1);
            l12_w32(o, p + 0xC4, l12_u32(o, p + 0x218));
            if (l12_c_00188610(o, p, &v)) return;
            if (l12_c_001749A0(o, p, v, 0, 0)) return;
        } else {
            l12_drift(o, p);
        }
        break;
    case 0:
        l12_w8(o, p + 6, st + 1u);
        if (l12_u8(o, p + 0x2F1) != 0) {
            if (l12_c_001749A0(o, p, 0x146, 0, 0x41000000u)) return;
        } else {
            if (l12_c_001749A0(o, p, 0x145, 0, 0x41000000u)) return;
        }
        l12_w32(o, SP_3A20, 0x3E06BCA2u);
        if (l12_c_0011E2A8(o, l12_u32(o, p + 0xC4), &r)) return;
        l12_w32(o, p + 0x2E0, L12_MUL(l12_u32(o, SP_3A20), r));
        if (l12_c_0011DE90(o, l12_u32(o, p + 0xC4), &r)) return;
        l12_w32(o, p + 0x2E8, L12_MUL(l12_u32(o, SP_3A20), r));
        break;
    default:
        break;
    }
    l12_w32(o, p + 0xB4, L12_ADD(l12_u32(o, p + 0xB4), 0xBE4CCCCDu));
    if (l12_c_00175900(o, p, 0)) return;
    l12_c_001796C0(o, p);
}

int em_level12_port_0016EF50(const EmLevel12PortHooks *h, uint32_t p, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_0016EF50(&o, p);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001831F0 (n): the player block 0x8102B0: n 0 -> +0x23F = 0, +0x24C =
 * -1; n 1 -> 2, 0; any other -> 2, 1.
 * ---------------------------------------------------------------------- */
void l12_001831F0(L12 *o, int32_t n)
{
    uint32_t p = 0x8102B0u;
    if (n == 0) {
        l12_w8(o, p + 0x23F, 0);
        l12_w32(o, p + 0x24C, 0xFFFFFFFFu);
    } else if (n == 1) {
        l12_w8(o, p + 0x23F, 2);
        l12_w32(o, p + 0x24C, 0);
    } else {
        l12_w8(o, p + 0x23F, 2);
        l12_w32(o, p + 0x24C, 1);
    }
}

int em_level12_port_001831F0(const EmLevel12PortHooks *h, int32_t n, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001831F0(&o, n);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001A96F0 (a, b): the box test of a pair: |b.x - a.x| <= box[0], |b.z -
 * a.z| <= box[2] and |b.y - a.y| <= 1 + box[1] (box = the word a +0x30,
 * re-read each time; 0011DF78) -> b +0x54 = 1 and the halfword 0x70003B86
 * = 0.
 * ---------------------------------------------------------------------- */
void l12_001A96F0(L12 *o, uint32_t a, uint32_t b)
{
    uint32_t g;
    {
        uint32_t ax = l12_u32(o, a + 0xB0);
        uint32_t bx = l12_u32(o, b + 0xB0);
        if (l12_c_0011DF78(o, L12_SUB(bx, ax), &g)) return;
    }
    if (!L12_LE(g, l12_u32(o, l12_u32(o, a + 0x30)))) return;
    {
        uint32_t bz = l12_u32(o, b + 0xB8);
        uint32_t az = l12_u32(o, a + 0xB8);
        if (l12_c_0011DF78(o, L12_SUB(bz, az), &g)) return;
    }
    if (!L12_LE(g, l12_u32(o, l12_u32(o, a + 0x30) + 8))) return;
    {
        uint32_t by = l12_u32(o, b + 0xB4);
        uint32_t ay = l12_u32(o, a + 0xB4);
        if (l12_c_0011DF78(o, L12_SUB(by, ay), &g)) return;
    }
    if (!L12_LE(g, L12_ADD(F_ONE, l12_u32(o, l12_u32(o, a + 0x30) + 4)))) return;
    l12_w16(o, b + 0x54, 1);
    l12_w16(o, 0x70003B86u, 0);
}

int em_level12_port_001A96F0(const EmLevel12PortHooks *h, uint32_t a, uint32_t b, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001A96F0(&o, a, b);
    return l12_end(&o);
}

/* A VU0 register load (lqc2) / store (sqc2): four words. */
static void l12_vload(L12 *o, uint32_t a, uint32_t v[4])
{
    a &= ~15u;
    for (unsigned k = 0; k < 4; k++) v[k] = l12_u32(o, a + 4u * k);
}

static void l12_vstore(L12 *o, uint32_t a, const uint32_t v[4])
{
    a &= ~15u;
    for (unsigned k = 0; k < 4; k++) l12_w32(o, a + 4u * k, v[k]);
}

/* ------------------------------------------------------------------------
 * 001C9570 (m, t, r, s): 001029C0(m), 00102C58(m, m, r); the rows 0..2 of
 * m scaled (x, y, z lanes) by s.x / s.y / s.z (VMULx / y / z with dest
 * xyz), row 3 stored back as read; 00102918(m, m, t).
 * ---------------------------------------------------------------------- */
void l12_001C9570(L12 *o, uint32_t m, uint32_t t, uint32_t r, uint32_t s)
{
    uint32_t row[4][4], sc[4];
    if (l12_c_001029C0(o, m)) return;
    if (l12_c_00102C58(o, m, m, r)) return;
    for (unsigned k = 0; k < 4; k++) l12_vload(o, m + 0x10u * k, row[k]);
    l12_vload(o, s, sc);
    for (int k = 0; k < 3; k++) {
        if (em_vu_vec_bits(EM_VU_MULBC, 0xEu, k, row[k], sc, 0, NULL, row[k]) != EM_EE_FLOAT_OK) {
            l12_latch(o, 0, EM_LEVEL12_PORT_FAULT_REGISTER);
            return;
        }
    }
    for (unsigned k = 0; k < 4; k++) l12_vstore(o, m + 0x10u * k, row[k]);
    l12_c_00102918(o, m, m, t);
}

int em_level12_port_001C9570(const EmLevel12PortHooks *h, uint32_t m, uint32_t t, uint32_t r, uint32_t s,
                             EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001C9570(&o, m, t, r, s);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001C6910 (o): 001C9570(o +0xD0, o +0xB0, o +0xC0, o +0x60), then
 * 001C9940(o +0x110, o +0xC, o +0xD0).
 * ---------------------------------------------------------------------- */
void l12_001C6910(L12 *o, uint32_t obj)
{
    l12_001C9570(o, obj + 0xD0, obj + 0xB0, obj + 0xC0, obj + 0x60);
    if (l12_failed(o)) return;
    l12_c_001C9940(o, obj + 0x110, (int32_t)l12_u8(o, obj + 0xC), obj + 0xD0);
}

int em_level12_port_001C6910(const EmLevel12PortHooks *h, uint32_t obj, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001C6910(&o, obj);
    return l12_end(&o);
}

/* One DMA ref tag at the write cursor of channel `sel` (the word at
 * base + sel * 4 + 0x10, base = the context D_00275670 read by the
 * caller): +3 = 0x30, +4 = addr, +0 = qwc (halfword), cursor += 0x10; the
 * cursor re-read for each store. */
static void l12_ref_tag(L12 *o, uint32_t e, uint32_t addr, uint32_t qwc)
{
    l12_w8(o, l12_u32(o, e + 0x10) + 3, 0x30);
    l12_w32(o, l12_u32(o, e + 0x10) + 4, addr);
    l12_w16(o, l12_u32(o, e + 0x10), qwc);
    l12_w32(o, e + 0x10, l12_u32(o, e + 0x10) + 0x10u);
}

/* ------------------------------------------------------------------------
 * 001D3F60 (sel, arg): 001D2090(sel, 0x23CC50), 001D1F80(sel, 1, 0),
 * 001D6B10(sel, D_00275678, 8, 8), 001D6BA0(sel, D_00275678, 8, 8, 2, 0),
 * 001D1FF0(sel, 3); the ref tags (0x816F40 + (context +0x9C << 7), 8),
 * (0x2514B0, 2) when 001D2910(0) is zero, and (arg +0x40, the halfword of
 * arg +4); 001D1F20(sel).
 * ---------------------------------------------------------------------- */
void l12_001D3F60(L12 *o, int32_t sel, uint32_t arg)
{
    int32_t r;
    if (l12_c_001D2090(o, sel, 0x23CC50u)) return;
    if (l12_c_001D1F80(o, sel, 1, 0)) return;
    if (l12_c_001D6B10(o, sel, l12_s32(o, 0x275678u), 8, 8)) return;
    if (l12_c_001D6BA0(o, sel, l12_s32(o, 0x275678u), 8, 8, 2, 0)) return;
    if (l12_c_001D1FF0(o, sel, 3)) return;
    {
        uint32_t base = l12_u32(o, 0x275670u);
        uint32_t row = l12_u32(o, base + 0x9C);
        l12_ref_tag(o, base + ((uint32_t)sel << 2), 0x816F40u + (row << 7), 8);
    }
    if (l12_c_001D2910(o, 0, &r)) return;
    if (r == 0) {
        uint32_t base = l12_u32(o, 0x275670u);
        l12_ref_tag(o, base + ((uint32_t)sel << 2), 0x2514B0u, 2);
    }
    {
        uint32_t base = l12_u32(o, 0x275670u);
        uint32_t n = l12_u32(o, arg + 4);
        l12_ref_tag(o, base + ((uint32_t)sel << 2), arg + 0x40, n);
    }
    l12_c_001D1F20(o, sel);
}

int em_level12_port_001D3F60(const EmLevel12PortHooks *h, int32_t sel, uint32_t arg, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001D3F60(&o, sel, arg);
    return l12_end(&o);
}

/* 001D40D0 (arg): 001D3F60(3, arg). */
int em_level12_port_001D40D0(const EmLevel12PortHooks *h, uint32_t arg, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001D3F60(&o, 3, arg);
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001CAFA0 (a0, a1): saved = the cursor context +0x1C (context =
 * D_00275670); 001D8C20(3), 001C7420(a0, 0x3F5, 3), 001D40D0(a1),
 * 001D1F20(3), 001D1FF0(3, 1); at the cursor (re-read) +3 = 0x60, +4 = 0,
 * +0 = 0 (halfword), cursor += 0x10; 001CB760(0x7635C0, 0xFFD000, saved,
 * 0x60), 001D8C20(0).
 * ---------------------------------------------------------------------- */
void l12_001CAFA0(L12 *o, uint32_t a0, uint32_t a1)
{
    uint32_t base = l12_u32(o, 0x275670u);
    uint32_t saved = l12_u32(o, base + 0x1C);
    if (l12_c_001D8C20(o, 3)) return;
    if (l12_c_001C7420(o, a0, 0x3F5, 3)) return;
    l12_001D3F60(o, 3, a1);
    if (l12_failed(o)) return;
    if (l12_c_001D1F20(o, 3)) return;
    if (l12_c_001D1FF0(o, 3, 1)) return;
    base = l12_u32(o, 0x275670u);
    l12_w8(o, l12_u32(o, base + 0x1C) + 3, 0x60);
    l12_w32(o, l12_u32(o, base + 0x1C) + 4, 0);
    l12_w16(o, l12_u32(o, base + 0x1C), 0);
    l12_w32(o, base + 0x1C, l12_u32(o, base + 0x1C) + 0x10u);
    if (l12_c_001CB760(o, 0x7635C0u, 0xFFD000u, saved, 0x60)) return;
    l12_c_001D8C20(o, 0);
}

int em_level12_port_001CAFA0(const EmLevel12PortHooks *h, uint32_t a0, uint32_t a1, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001CAFA0(&o, a0, a1);
    return l12_end(&o);
}

/* 001CB060 (obj): 001CAFA0(obj, obj +0x44). */
int em_level12_port_001CB060(const EmLevel12PortHooks *h, uint32_t obj, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001CAFA0(&o, obj, l12_u32(&o, obj + 0x44));
    return l12_end(&o);
}

/* ------------------------------------------------------------------------
 * 001E8B40 (n): n 1 -> the byte +5 of the record at the word +0x58 of
 * D_00275C20 = 1; n 0 -> the same through the word +0xA0B8; others
 * nothing.
 * ---------------------------------------------------------------------- */
void l12_001E8B40(L12 *o, int32_t n)
{
    if (n == 1) {
        uint32_t t = l12_u32(o, 0x275C20u);
        l12_w8(o, l12_u32(o, t + 0x58) + 5, 1);
    } else if (n == 0) {
        uint32_t t = l12_u32(o, 0x275C20u);
        l12_w8(o, l12_u32(o, t + 0xA0B8u) + 5, 1);
    }
}

int em_level12_port_001E8B40(const EmLevel12PortHooks *h, int32_t n, EmLevel12PortFault *fault)
{
    L12 o;
    if (l12_begin(&o, h, fault)) return -1;
    l12_001E8B40(&o, n);
    return l12_end(&o);
}
