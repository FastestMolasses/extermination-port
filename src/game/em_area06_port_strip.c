/* 001CE860, the strip packet of the class 0x8000003B strip nodes (0021A500
 * draws them with it; see em_area06_port.h, docs/AREA06_PORT.md). The
 * decomp holds only its assembly (undecompiled); this translation follows
 * the original instructions (read locally, never reproduced). The VU0
 * macro steps use em_ee_float.h's measured forms (the same forms as AREA00's
 * line packet 001CD940, em_area00_fx_gs.c); a form it refuses latches fault
 * 6. The conventions are those of em_area06_port.c.
 */
#include "em_area06_port_internal.h"

#define D_00275670 0x00275670u /* the render context pointer */

static const uint32_t A6_VF0[4] = { 0, 0, 0, 0x3F800000u };

static void a6_vu(A6 *o, em_vu_op op, unsigned dest, int bc, const uint32_t fs[4], const uint32_t ft[4], uint32_t q,
                  const uint32_t acc[4], uint32_t dst[4])
{
    if (a6_failed(o)) return;
    if (em_vu_vec_bits(op, dest, bc, fs, ft, q, acc, dst) != EM_EE_FLOAT_OK)
        a6_latch(o, 0x001CE860u, EM_AREA06_PORT_FAULT_FLOAT_FORM);
}

/* A quadword through lqc2 / sqc2: four words in lane order. */
static void a6_ldv(A6 *o, uint32_t a, uint32_t v[4])
{
    for (uint32_t k = 0; k < 4; k++) v[k] = a6_u32(o, (a & ~15u) + 4 * k);
}

static void a6_stv(A6 *o, uint32_t a, const uint32_t v[4])
{
    for (uint32_t k = 0; k < 4; k++) a6_w32(o, (a & ~15u) + 4 * k, v[k]);
}

/* out = m[0] v.x + m[1] v.y + m[2] v.z + m[3] vf0.w (the accumulate chain
 * ending in the vf0.w form). */
static void a6_transform(A6 *o, const uint32_t m[4][4], const uint32_t v[4], uint32_t out[4])
{
    uint32_t acc[4] = { 0, 0, 0, 0 };
    a6_vu(o, EM_VU_MULABC, 15, 0, m[0], v, 0, NULL, acc);
    a6_vu(o, EM_VU_MADDABC, 15, 1, m[1], v, 0, acc, acc);
    a6_vu(o, EM_VU_MADDABC, 15, 2, m[2], v, 0, acc, acc);
    a6_vu(o, EM_VU_MADDBC, 15, 3, m[3], A6_VF0, 0, acc, out);
}

/* The clip judgment of x, y, z against |w| (flag bits +x, -x, +y, -y, +z,
 * -z): denormals as zero, then magnitude compares; a lane with exponent
 * 255 is not measured and latches fault 6. */
static uint32_t a6_clipw(A6 *o, const uint32_t v[4])
{
    if (a6_failed(o)) return 0;
    for (int i = 0; i < 4; ++i)
        if (((v[i] >> 23) & 0xFFu) == 0xFFu) {
            a6_latch(o, 0x001CE860u, EM_AREA06_PORT_FAULT_FLOAT_FORM);
            return 0;
        }
    const uint32_t w = em_eei_daz(v[3]) & 0x7FFFFFFFu;
    uint32_t f = 0;
    for (int k = 0; k < 3; ++k) {
        const uint32_t x = em_eei_daz(v[k]);
        if ((x & 0x7FFFFFFFu) > w) f |= (x >> 31) ? 2u << (2 * k) : 1u << (2 * k);
    }
    return f;
}

/* vdiv Q = vf0.w / w (the reciprocal form (3, 3)). */
static uint32_t a6_vrcp(A6 *o, uint32_t w)
{
    uint32_t q = 0;
    if (!a6_failed(o) && em_vu_div_bits(0x3F800000u, w, 3, 3, &q) != EM_EE_FLOAT_OK)
        a6_latch(o, 0x001CE860u, EM_AREA06_PORT_FAULT_FLOAT_FORM);
    return q;
}

/* An unsigned word as a float, the compiler's idiom: CVT.S.W of the word,
 * or for a word with the top bit set, of (word >> 1 | word & 1) doubled. */
static uint32_t a6_u2f(uint32_t v)
{
    if ((int32_t)v >= 0) return A6_CVT(v);
    uint32_t h = A6_CVT((v >> 1) | (v & 1u));
    return A6_ADD(h, h);
}

/* ------------------------------------------------------------------------
 * 001CE860 (asm; from the instructions). The frame is 0x100: a0 / a1 are
 * kept at sp - 0x44 / - 0x48, two 16-byte point slots at sp - 0x40 / -
 * 0x30 (alternating), the clip-space spill at sp - 0x20 and the two 1/w
 * floats at sp - 8 / - 4.
 *  - The cull matrix K = 001CD370(0) (four quadwords), the fog row CA0 =
 *    *D_00275670 +0xA0, the view matrix at 0x70003AC0; the 64 bytes at
 *    0x70003A40 are copied to 0x70003400 and its row 3 set to (0, 0, 0, 1);
 *    key = 001CCF70(points) (the first point's depth key, a stub here);
 *    the packet p = 001CB5F0(0x28F700 + a0
 *    << 15 + 0x4D3EC0, key, 5 n + 4) gets its GIF header (the tag word at
 *    +0x20, n at +0x30).
 *  - Each point i: the clip judgment of K p selects the ADC bit 0x8000; the
 *    projection (as 001CD940: Q = 1 / w, x, y *= Q, w -= 3.0, Q = 1 / w, z
 *    *= Q, the fog lane from CA0, all lanes to 12.4) into the slot; 1 / the
 *    clip-space w into the float slot; two 0x50-byte vertex records (the
 *    colour as integers through 001281C0, the strip parameter 0 / 1
 *    alternating, the projected x, y, z, and the fog word with the ADC
 *    bit). From the second point on, the screen-space step between the two
 *    slots (as unsigned x / y) is turned into the perpendicular of half
 *    width `width` (1 / its length, 0x7000368C), carried through the matrix
 *    0x70003400 (001026A0 on 0x70003630) and scaled by 16 / w into integers
 *    (0x70003630 / 0x70003634), which widen the previous record's two
 *    vertices (+ and -), and on the last point the current record's too.
 *  - Finally 001CB900(the same context address, key, a1).
 * ---------------------------------------------------------------------- */
static void a6_001CE860(A6 *o, int32_t a0, int32_t a1, uint32_t points, uint32_t colour, int32_t n, uint64_t tag,
                        uint32_t width, uint32_t sp)
{
    const uint32_t fr = sp - 0x100;
    uint32_t k[4][4], m[4][4], ca0[4];
    uint32_t mat = 0;
    int32_t key = 0;
    a6_w32(o, fr + 0xBC, (uint32_t)a0);
    a6_w32(o, fr + 0xB8, (uint32_t)a1);
    if (a6_c_001CD370(o, 0, &mat)) return;
    for (uint32_t r = 0; r < 4; r++) a6_ldv(o, mat + 16 * r, k[r]);
    const int32_t words = n * 5 + 3;
    a6_ldv(o, a6_u32(o, D_00275670) + 0xA0, ca0);
    for (uint32_t r = 0; r < 4; r++) a6_ldv(o, 0x70003AC0u + 16 * r, m[r]);
    for (uint32_t r = 0; r < 4; r++) {
        uint8_t q[16];
        a6_q(o, 0x70003A40u + 16 * r, q);
        a6_wq(o, 0x70003400u + 16 * r, q);
    }
    a6_w32(o, 0x70003430u, 0);
    a6_w32(o, 0x70003434u, 0);
    a6_w32(o, 0x70003438u, 0);
    a6_w32(o, 0x7000343Cu, 0x3F800000u);
    if (a6_c_001CCF70(o, points, &key)) return;
    uint32_t ctx = 0x0028F700u + ((uint32_t)a6_u32(o, fr + 0xBC) << 15) + 0x004D3EC0u;
    uint32_t p = 0;
    if (a6_c_001CB5F0(o, ctx, key, words + 1, &p)) return;
    {
        const uint8_t zero[16] = { 0 };
        a6_wq(o, p, zero);
    }
    a6_w32(o, p + 0xC, (uint32_t)words | 0x50000000u);
    uint64_t v64 = UINT64_C(0x1000000000000001);
    memcpy(a6_at(o, p + 0x10, 8), &v64, 8);
    v64 = 0xE;
    memcpy(a6_at(o, p + 0x18, 8), &v64, 8);
    memcpy(a6_at(o, p + 0x20, 8), &tag, 8);
    v64 = 6;
    memcpy(a6_at(o, p + 0x28, 8), &v64, 8);
    v64 = (uint64_t)(int64_t)n | UINT64_C(0x502E400000008000);
    memcpy(a6_at(o, p + 0x30, 8), &v64, 8);
    v64 = 0x42421;
    memcpy(a6_at(o, p + 0x38, 8), &v64, 8);
    uint32_t rec = p + 0x40, prev = 0, side = 0, param = 0;
    for (int32_t i = 0; i < n; i++) {
        uint32_t v[4], c[4];
        const uint32_t pt = points + ((uint32_t)i << 4);
        a6_ldv(o, pt, v);
        a6_transform(o, k, v, c);
        uint32_t adc = (a6_clipw(o, c) & 0x3Fu) ? 0x8000u : 0u;
        const uint32_t slot = fr + 0xC0 + (side << 4);
        a6_ldv(o, pt, v);
        a6_transform(o, m, v, c);
        uint32_t q = a6_vrcp(o, c[3]);
        a6_stv(o, fr + 0xE0, c);
        a6_vu(o, EM_VU_MULQ, 12, EM_VU_NO_BC, c, NULL, q, NULL, c);
        {
            static const uint32_t three_x[4] = { 0x40400000u, 0, 0, 0 };
            a6_vu(o, EM_VU_SUBBC, 1, 0, c, three_x, 0, NULL, c);
        }
        q = a6_vrcp(o, c[3]);
        a6_vu(o, EM_VU_MULQ, 2, EM_VU_NO_BC, c, NULL, q, NULL, c);
        {
            uint32_t acc[4] = { 0, 0, 0, 0 };
            a6_vu(o, EM_VU_MULABC, 1, 2, A6_VF0, ca0, 0, NULL, acc);
            a6_vu(o, EM_VU_MADDBC, 1, 3, ca0, c, 0, acc, c);
            c[3] = em_vu_min_bits(c[3], ca0[0]);
            c[3] = em_vu_max_bits(c[3], 0);
        }
        for (int l = 0; l < 4; l++) c[l] = em_vu_ftoi4_bits(c[l]);
        if (a6_failed(o)) return;
        a6_stv(o, slot, c);
        a6_w32(o, fr + 0xF8 + (side << 2), A6_DIV(0x3F800000u, a6_u32(o, fr + 0xEC)));
        for (uint32_t l = 0; l < 4; l++) {
            int32_t ci = 0;
            if (a6_c_001281C0(o, fl(a6_u32(o, colour + 4 * l)), &ci)) return;
            a6_w32(o, rec + 4 * l, (uint32_t)ci);
        }
        const uint32_t cur = fr + (side << 4);
        a6_w32(o, rec + 0x10, param);
        a6_w32(o, rec + 0x14, 0);
        a6_w32(o, rec + 0x18, 0x3F800000u);
        a6_w32(o, rec + 0x20, a6_u32(o, cur + 0xC0));
        a6_w32(o, rec + 0x24, a6_u32(o, cur + 0xC4));
        a6_w32(o, rec + 0x28, a6_u32(o, cur + 0xC8));
        a6_w32(o, rec + 0x2C, adc | a6_u32(o, cur + 0xCC));
        a6_w32(o, rec + 0x30, param);
        a6_w32(o, rec + 0x34, 0x3F800000u);
        a6_w32(o, rec + 0x38, 0x3F800000u);
        a6_w32(o, rec + 0x40, a6_u32(o, cur + 0xC0));
        a6_w32(o, rec + 0x44, a6_u32(o, cur + 0xC4));
        a6_w32(o, rec + 0x48, a6_u32(o, cur + 0xC8));
        a6_w32(o, rec + 0x4C, adc | a6_u32(o, cur + 0xCC));
        if (i != 0) {
            const uint32_t old = fr + ((1u - side) << 4);
            uint32_t x1 = a6_u2f(a6_u32(o, cur + 0xC0));
            uint32_t x0 = a6_u2f(a6_u32(o, old + 0xC0));
            uint32_t dx = A6_SUB(x1, x0);
            uint32_t y1w = a6_u32(o, cur + 0xC4);
            a6_w32(o, 0x70003634u, dx);
            uint32_t y1 = a6_u2f(y1w);
            uint32_t y0 = a6_u2f(a6_u32(o, old + 0xC4));
            uint32_t ny = A6_NEG(A6_SUB(y1, y0));
            a6_w32(o, 0x70003630u, ny);
            uint32_t dxr = a6_u32(o, 0x70003634u);
            float len = 0.0f;
            if (a6_c_0011E748(o, fl(A6_MADD(A6_MUL(ny, ny), dxr, dxr)), &len)) return;
            uint32_t inv = A6_DIV(0x3F800000u, a6_bits(len));
            uint32_t a = a6_u32(o, 0x70003630u);
            uint32_t s = A6_MUL(width, inv);
            uint32_t rw = a6_u32(o, fr + 0xF8 + (side << 2));
            a6_w32(o, 0x7000368Cu, s);
            a6_w32(o, 0x70003630u, A6_MUL(a, s));
            a6_w32(o, 0x70003634u, A6_MUL(a6_u32(o, 0x70003634u), s));
            a6_w32(o, 0x70003638u, rw);
            a6_w32(o, 0x7000363Cu, 0x3F800000u);
            if (a6_c_001026A0(o, 0x70003630u, 0x70003400u, 0x70003630u)) return;
            for (uint32_t l = 0; l < 2; l++) {
                uint32_t w = a6_u32(o, fr + 0xF8 + (side << 2));
                uint32_t d = a6_u32(o, 0x70003630u + 4 * l);
                int32_t di = 0;
                if (a6_c_001281C0(o, fl(A6_MUL(A6_MUL(0x41800000u, d), w)), &di)) return;
                a6_w32(o, 0x70003630u + 4 * l, (uint32_t)di);
            }
            for (int twice = 0; twice < 2; twice++) {
                const uint32_t r = twice ? rec : prev;
                if (twice && i != n - 1) break;
                uint32_t dxi = a6_u32(o, 0x70003630u);
                a6_w32(o, r + 0x20, a6_u32(o, r + 0x20) + dxi);
                uint32_t dyi = a6_u32(o, 0x70003634u);
                a6_w32(o, r + 0x24, a6_u32(o, r + 0x24) + dyi);
                dxi = a6_u32(o, 0x70003630u);
                a6_w32(o, r + 0x40, a6_u32(o, r + 0x40) - dxi);
                uint32_t y = a6_u32(o, r + 0x44);
                dyi = a6_u32(o, 0x70003634u);
                a6_w32(o, r + 0x44, y - dyi);
            }
        }
        side = 1u - side;
        param = A6_SUB(0x3F800000u, param);
        prev = rec;
        rec += 0x50;
    }
    if (a6_failed(o)) return;
    uint32_t ctx2 = 0x0028F700u + ((uint32_t)a6_u32(o, fr + 0xBC) << 15) + 0x004D3EC0u;
    int32_t a1w = (int32_t)a6_u32(o, fr + 0xB8);
    a6_c_001CB900(o, ctx2, key, a1w);
}

int em_area06_port_001CE860(const EmArea06PortHooks *h, int32_t a0, int32_t a1, uint32_t points, uint32_t colour,
                            int32_t count, uint64_t tag, float width, uint32_t sp, EmArea06PortFault *fault)
{
    A6 o;
    if (a6_begin(&o, h, fault)) return -1;
    a6_001CE860(&o, a0, a1, points, colour, count, tag, a6_bits(width), sp);
    return a6_end(&o);
}
