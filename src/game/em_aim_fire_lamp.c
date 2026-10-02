/* em_aim_fire_lamp.c - the gun lamp (em_aim_fire_lamp.h, docs/AIM_FIRE.md
 * section 10), translated from the original instructions. */
#include "game/em_aim_fire_lamp.h"
#include "game/em_ee_float.h"
#include <string.h>

typedef struct { EmAimFireTarget *h; uint32_t entry, sp; } Run;

#define ONE      0x3F800000u
#define CTX_PTR  0x00275670u     /* D_00275670 (gp-relative): the render context */
#define BONES    0x00275B40u     /* D_00275B40 (gp-relative): the bone table */
#define LIBRARY  0x0028A56Cu     /* D_0028A56C: the common model bank */
#define HIT      0x700031B0u     /* 0019A570's point */
#define HIT_FACE 0x700031D0u     /* 0019A570's face record */
#define EYE      0x008105D0u     /* D_008105D0 */
#define AREA     0x00810700u     /* D_00810700 / D_00810701 */
#define PAGE     0x007635C0u     /* D_007635C0 */
#define FLARE    0x002487E0u     /* D_002487E0: 00187690's source block */
#define LEVEL_KERNEL 0x00237180u
#define CLIP_KERNEL  0x00239C90u
#define TEMPLATE_FLAT 0x00816640u
#define TEMPLATE_GOURAUD 0x00816740u
#define SPAD_3400 0x70003400u
#define SPAD_3440 0x70003440u
#define SPAD_3470 0x70003470u
#define SPAD_3AC0 0x70003AC0u

#define ENTER(fn, frame) Run run = {h, fn, h ? h->sp - (frame) : 0}, *r = &run; \
    if (!h || h->fault) return -1
#define DONE() return r->h->fault ? -1 : 0

static void fault(Run *r, int code, uint32_t address)
{
    if (!r->h->fault) {
        r->h->fault = code; r->h->fault_function = r->entry;
        r->h->fault_address = address;
    }
}
static uint8_t *memory(Run *r, uint32_t address, uint32_t size, int write)
{
    uint8_t *p;
    if (r->h->fault) return NULL;
    if (!r->h->map) { fault(r, 1, address); return NULL; }
    p = r->h->map(r->h->context, address, size, write);
    if (!p) fault(r, 2, address);
    return p;
}
static uint32_t readn(Run *r, uint32_t a, unsigned n)
{
    uint8_t *p = memory(r, a, n, 0); uint32_t v = 0;
    if (p) for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint32_t word(Run *r, uint32_t a) { return readn(r, a, 4); }
static void put(Run *r, uint32_t a, uint64_t v, unsigned n)
{
    uint8_t *p = memory(r, a, n, 1);
    if (!p) return;
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8 * i));
    if (r->h->store) r->h->store(r->h->context, a, n);
}
static void store(Run *r, uint32_t a, uint32_t v) { put(r, a, v, 4); }
/* LQ / SQ: one quadword, read whole before it is written. */
static void copy_qword(Run *r, uint32_t dst, uint32_t src)
{
    uint8_t q[16];
    const uint8_t *s = memory(r, src, 16, 0);
    if (!s) return;
    memcpy(q, s, 16);
    uint8_t *d = memory(r, dst, 16, 1);
    if (!d) return;
    memcpy(d, q, 16);
    if (r->h->store) r->h->store(r->h->context, dst, 16);
}
static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }
static EmAimFireTargetCall call(Run *r, uint32_t fn, unsigned na, unsigned nf, const uint64_t *a, const uint32_t *f)
{
    EmAimFireTargetCall c;
    memset(&c, 0, sizeof c);
    c.function = fn; c.sp = r->sp; c.na = na; c.nf = nf;
    for (unsigned i = 0; i < na; ++i) c.a[i] = a[i];
    for (unsigned i = 0; i < nf; ++i) c.f[i] = f[i];
    if (r->h->fault) return c;
    if (!r->h->call) fault(r, 1, fn);
    else if (r->h->call(r->h->context, &c) < 0) fault(r, 3, fn);
    return c;
}
static uint32_t c0(Run *r, uint32_t fn) { return (uint32_t)call(r, fn, 0, 0, NULL, NULL).v0; }
static uint32_t c1(Run *r, uint32_t fn, uint64_t a)
{ const uint64_t v[1] = {a}; return (uint32_t)call(r, fn, 1, 0, v, NULL).v0; }
static uint32_t c2(Run *r, uint32_t fn, uint64_t a, uint64_t b)
{ const uint64_t v[2] = {a, b}; return (uint32_t)call(r, fn, 2, 0, v, NULL).v0; }
static uint32_t c3(Run *r, uint32_t fn, uint64_t a, uint64_t b, uint64_t c)
{ const uint64_t v[3] = {a, b, c}; return (uint32_t)call(r, fn, 3, 0, v, NULL).v0; }
static void c2f(Run *r, uint32_t fn, uint64_t a, uint64_t b, uint32_t f)
{ const uint64_t v[2] = {a, b}; const uint32_t g[1] = {f}; (void)call(r, fn, 2, 1, v, g); }
static uint32_t unary(Run *r, uint32_t fn, uint32_t f)
{ const uint32_t g[1] = {f}; return call(r, fn, 0, 1, NULL, g).f0; }
/* 00128250(f12): the soft-float float -> unsigned, its v0 (64 bits). */
static uint64_t soft_v0(Run *r, uint32_t f)
{ const uint32_t g[1] = {f}; return call(r, 0x00128250u, 0, 1, NULL, g).v0; }
static uint32_t soft_u(Run *r, uint32_t f) { return (uint32_t)soft_v0(r, f); }
static uint32_t to_int(Run *r, uint32_t f)                        /* float_to_int 001281C0 */
{ const uint32_t g[1] = {f}; return (uint32_t)call(r, 0x001281C0u, 0, 1, NULL, g).v0; }
static void render_param(Run *r, int32_t mode, uint32_t f12, uint32_t f13)   /* 0021B9A0 */
{ const uint64_t v[1] = {sx((uint32_t)mode)}; const uint32_t g[2] = {f12, f13}; (void)call(r, 0x0021B9A0u, 1, 2, v, g); }

/* A DMA tag at the cursor, written as the originals do (ID byte +3, the
 * address word +4, the QWC halfword +0, each through the cursor re-read),
 * then the cursor advanced by `advance`. Returns the tag's address. */
static uint32_t tag(Run *r, uint32_t cw, uint32_t id, uint32_t address, uint32_t qwc, uint32_t advance)
{
    put(r, word(r, cw) + 3u, id, 1);
    store(r, word(r, cw) + 4u, address);
    put(r, word(r, cw), qwc, 2);
    const uint32_t t = word(r, cw);
    store(r, cw, t + advance);
    return t;
}

/* ------------------------------------------------------------ 00187780 */

int em_aim_fire_lamp_00187780(EmAimFireTarget *h, uint32_t node, int32_t a1, int32_t a2)
{
    ENTER(0x00187780u, 0x160u);
    const uint32_t sp = r->sp;
    /* f20 the flare size, f22 / f23 the flare's two floats, s1 its word;
     * a mode / variant outside 0 / 1 leaves them as the caller's registers
     * were (not reached: 00188ED0 passes 0 or 1 for both). */
    uint32_t f20 = 0, f22 = 0, f23 = 0;
    uint64_t word64 = 0;
    if ((a2 != 0 && a2 != 1) || (a1 != 0 && a1 != 1)) {
        fault(r, 4, (uint32_t)a2);                        /* register contents not modelled */
        return -1;
    }
    if (a2 == 0) {
        f20 = 0x42800000u; f22 = 0x41880000u; f23 = 0x40600000u;     /* 64, 17, 3.5 */
        word64 = UINT64_C(0x20045D05554221F6);
        for (unsigned k = 0; k < 3; ++k) store(r, sp + 0xB8u - 4u * k, 0x41800000u);   /* 16 */
        const uint32_t v = c0(r, 0x00122BB8u);
        uint32_t f = em_ee_div_bits(em_ee_cvt_s_w_bits(v), 0x4F000000u);
        f = em_ee_add_bits(0x40A00000u, em_ee_mul_bits(0x40400000u, f));            /* 5 + 3 r */
        for (unsigned k = 0; k < 3; ++k) store(r, sp + 0xC8u - 4u * k, f);
    } else {
        f20 = 0x42000000u; f22 = 0x41880000u; f23 = 0x40600000u;     /* 32, 17, 3.5 */
        word64 = UINT64_C(0x20048D0599422050);
        for (unsigned k = 0; k < 3; ++k) store(r, sp + 0xB8u - 4u * k, 0x41000000u);   /* 8 */
        const uint32_t v = c0(r, 0x00122BB8u);
        const uint32_t f = em_ee_add_bits(0x40000000u, em_ee_div_bits(em_ee_cvt_s_w_bits(v), 0x4F000000u));
        for (unsigned k = 0; k < 3; ++k) store(r, sp + 0xC8u - 4u * k, f);
    }
    /* The light matrix sp+0xE0: identity, the bone's three rows, a
     * quarter turn about Y, translated to the node's +0xB0. */
    c1(r, 0x001029C0u, sp + 0xE0u);
    c2(r, 0x00102948u, sp + 0xE0u, word(r, word(r, BONES)) + 0x90u);
    c2(r, 0x00102948u, sp + 0xF0u, word(r, word(r, BONES)) + 0xA0u);
    c2(r, 0x00102948u, sp + 0x100u, word(r, word(r, BONES)) + 0xB0u);
    c1(r, 0x001029C0u, sp + 0x120u);
    c2f(r, 0x00102BB0u, sp + 0x120u, sp + 0x120u, 0x3FC90FDBu);
    c3(r, 0x001026D0u, sp + 0xE0u, sp + 0xE0u, sp + 0x120u);
    c3(r, 0x00102918u, sp + 0xE0u, sp + 0xE0u, node + 0xB0u);
    store(r, sp + 0x90u, 0);
    store(r, sp + 0x94u, 0);
    store(r, sp + 0x98u, 0x437A0000u);                                 /* 250 */
    store(r, sp + 0x9Cu, ONE);
    c3(r, 0x001026A0u, sp + 0x90u, sp + 0xE0u, sp + 0x90u);
    uint32_t f21, f24;
    {
        const uint64_t v[4] = {sp + 0x110u, sp + 0x90u, 6, 0};
        const uint32_t hit = (uint32_t)call(r, 0x0019A570u, 4, 0, v, NULL).v0;
        if (r->h->fault) return -1;
        if (hit) {
            c3(r, 0x001028D0u, sp + 0x90u, HIT, sp + 0x110u);
            const uint32_t y = word(r, sp + 0x94u), x = word(r, sp + 0x90u), z = word(r, sp + 0x98u);
            const uint32_t acc = em_ee_adda_bits(em_ee_mul_bits(x, x), em_ee_mul_bits(y, y));
            const uint32_t len = unary(r, 0x0011E748u, em_ee_madd_bits(acc, z, z));
            f24 = f21 = len;
            if (!em_ee_c_le_bits(f24, 0x41880000u)) f24 = 0x41880000u;          /* 17 */
            if (!em_ee_c_le_bits(f21, 0x437A0000u)) f21 = 0x437A0000u;          /* 250 */
            f24 = em_ee_div_bits(f24, f22);
            c2(r, 0x00102948u, sp + 0xD0u, word(r, HIT_FACE) + 0x24u);
        } else {
            f21 = 0x437A0000u;
            f24 = ONE;
            c2f(r, 0x00102900u, sp + 0xD0u, sp + 0x100u, 0xBF800000u);
        }
    }
    f22 = em_ee_mul_bits(f22, f24);
    f23 = em_ee_mul_bits(f23, f24);
    f20 = em_ee_mul_bits(f20, f24);
    if (a2 == 0) {
        c3(r, 0x001028D0u, sp + 0x90u, sp + 0x110u, EYE);
        c2(r, 0x00102760u, sp + 0x90u, sp + 0x90u);
        c2(r, 0x00102760u, sp + 0xA0u, sp + 0x100u);
        const uint64_t v[2] = {sp + 0x90u, sp + 0xA0u};
        const uint32_t dot = call(r, 0x00102738u, 2, 0, v, NULL).f0;
        const uint32_t d = unary(r, 0x0011DF78u, dot);
        if (!em_ee_c_le_bits(d, 0x3F333333u)) {                           /* 0.7 */
            const uint32_t t = em_ee_div_bits(em_ee_sub_bits(d, 0x3F333333u), 0x3E99999Au);
            c2f(r, 0x00103230u, sp + 0xB0u, sp + 0xB0u, em_ee_sub_bits(ONE, em_ee_mul_bits(0x3F4CCCCDu, t)));
        }
    }
    render_param(r, 2, 0, 0xC30C0000u);                                   /* -140 */
    render_param(r, 3, 0, 0x43E10000u);                                   /* 450 */
    {
        const uint32_t size = to_int(r, f20);
        const uint64_t v[5] = {sp + 0xE0u, sp + 0xB0u, sp + 0xC0u, sx(size), word64};
        const uint32_t g[3] = {f22, 0x3DCCCCCDu, f23};
        (void)call(r, 0x00187690u, 5, 3, v, g);
    }
    if (!(c0(r, 0x001B0070u) & 0x20000000u)) {
        const uint64_t v[4] = {sp + 0xE0u, sp + 0xB0u, sp + 0xC0u, sp + 0xD0u};
        const uint32_t g[1] = {f21};
        (void)call(r, 0x001D9530u, 4, 1, v, g);
    }
    if (a2 == 1) {
        f23 = em_ee_mul_bits(f23, 0x3F400000u);                           /* 0.75 */
        c2f(r, 0x00103230u, sp + 0xB0u, sp + 0xB0u, 0x40000000u);
        c2f(r, 0x00103230u, sp + 0xC0u, sp + 0xC0u, ONE);
        const uint32_t size = to_int(r, f20);
        const uint64_t v[5] = {sp + 0xE0u, sp + 0xB0u, sp + 0xC0u, sx(size), UINT64_C(0x20045D05554221F6)};
        const uint32_t g[3] = {f22, 0x3DCCCCCDu, f23};
        (void)call(r, 0x00187690u, 5, 3, v, g);
    }
    render_param(r, 1, 0, 0);
    DONE();
}

/* ------------------------------------------------------------ 00187690 */

int em_aim_fire_lamp_00187690(EmAimFireTarget *h, uint32_t matrix, uint32_t b, uint32_t c, int32_t size,
                              uint64_t word64, uint32_t f12, uint32_t f13, uint32_t f14)
{
    ENTER(0x00187690u, 0xA0u);
    const uint32_t sp = r->sp;
    copy_qword(r, FLARE + 0x20u, b);                                      /* D_00248800 */
    copy_qword(r, FLARE + 0x30u, c);                                      /* D_00248810 */
    store(r, FLARE + 0x08u, f12);                                         /* D_002487E8 */
    store(r, FLARE + 0x40u, f13);                                         /* D_00248820 */
    store(r, FLARE + 0x44u, f13);
    store(r, FLARE + 0x50u, f14);                                         /* D_00248830 */
    store(r, FLARE + 0x54u, f14);
    store(r, sp + 0x30u, 0);
    store(r, FLARE + 0x80u, (uint32_t)size);                              /* D_00248860 */
    store(r, sp + 0x34u, 0);
    store(r, sp + 0x38u, f12);
    put(r, FLARE + 0x70u, word64, 8);                                     /* D_00248850 */
    store(r, sp + 0x3Cu, ONE);
    c3(r, 0x001026A0u, sp + 0x30u, matrix, sp + 0x30u);
    const uint32_t key = c1(r, 0x001CCF70u, sp + 0x30u);
    {
        const uint64_t v[2] = {sp + 0x40u, matrix};
        const uint32_t g[2] = {ONE, 0x3DCCCCCDu};
        (void)call(r, 0x001CFA60u, 2, 2, v, g);
    }
    {
        const uint64_t v[5] = {sx(key), 1, FLARE, sp + 0x40u, 1};
        (void)call(r, 0x001CFBE0u, 5, 0, v, NULL);
    }
    DONE();
}

/* ------------------------------------------------------------ 001D9530 */

int em_aim_fire_lamp_001D9530(EmAimFireTarget *h, uint32_t matrix)
{
    ENTER(0x001D9530u, 0xA0u);
    const uint32_t sp = r->sp;
    copy_qword(r, sp + 0x70u, 0x00253190u);
    copy_qword(r, sp + 0x80u, 0x002531A0u);
    copy_qword(r, sp + 0x90u, 0x002531B0u);
    const uint32_t m10 = c2(r, 0x001C6120u, sx(word(r, LIBRARY)), 0x10);
    const uint32_t m11 = c2(r, 0x001C6120u, sx(word(r, LIBRARY)), 0x11);
    const uint32_t m16 = c2(r, 0x001C6120u, sx(word(r, LIBRARY)), 0x16);
    const uint32_t list = word(r, word(r, CTX_PTR) + 0x1Cu);            /* channel 3's cursor */
    uint32_t f20;
    if (c0(r, 0x001B0070u) & 0x80u) {
        f20 = 0x44228000u;                                                /* 650 */
    } else {
        const uint32_t key = (readn(r, AREA, 1) << 8) + readn(r, AREA + 1u, 1);
        f20 = key == 0x1200u || key == 0x201u || key == 0x300u || key == 0x1000u ? 0x44228000u : 0x42F00000u;
    }
    {
        const uint64_t v[2] = {3, 0};
        (void)call(r, 0x001DA290u, 2, 0, v, NULL);
    }
    const uint32_t colour[3] = {sp + 0x80u, sp + 0x70u, sp + 0x90u};
    const uint32_t model[3] = {m10, m11, m16};
    for (unsigned k = 0; k < 3; ++k) {
        const uint64_t v[4] = {matrix, colour[k], model[k], k == 2 ? 1u : 0u};
        const uint32_t g[1] = {f20};
        (void)call(r, 0x001D91A0u, 4, 1, v, g);
    }
    const uint32_t ctx = word(r, CTX_PTR);
    (void)tag(r, ctx + 0x1Cu, 0x60u, 0, 0, 0x10u);                        /* RET */
    {
        const uint64_t v[4] = {PAGE, 0, list, ctx};
        (void)call(r, 0x001CB760u, 4, 0, v, NULL);
    }
    DONE();
}

/* ------------------------------------------------------------ 001D91A0 */

int em_aim_fire_lamp_001D91A0(EmAimFireTarget *h, uint32_t matrix, uint32_t colour, uint32_t model, int32_t flag,
                              uint32_t f12)
{
    ENTER(0x001D91A0u, 0x160u);
    const uint32_t sp = r->sp;
    const uint32_t scale = em_ee_mul_bits(0x3DCCCCCDu, em_ee_div_bits(f12, 0x41A00000u));   /* 0.1 * f / 20 */
    c1(r, 0x001029C0u, sp + 0xA0u);
    store(r, sp + 0xA0u, scale);
    store(r, sp + 0xB4u, scale);
    store(r, sp + 0xC8u, scale);
    c3(r, 0x001026D0u, sp + 0x60u, matrix, sp + 0xA0u);
    /* CNT 5: FLUSH, STCYCL 1, 1, UNPACK V4-32 of 4 rows to 0x3F5, then the
     * 0x70003440 rows (row 3: 001028B8 of the colour and D_0026E540). */
    const uint32_t cw = word(r, CTX_PTR) + 0x1Cu;
    const uint32_t first = tag(r, cw, 0x10u, 0, 5, 0x60u);
    for (uint32_t a = SPAD_3400; a <= 0x7000346Cu; a += 4) store(r, a, 0);
    c3(r, 0x001028B8u, SPAD_3470, colour, 0x0026E540u);
    const uint32_t s3 = first + 0x10u;
    store(r, s3, 0x11000000u);
    store(r, s3 + 4u, 0x01000101u);
    store(r, s3 + 8u, 0);
    store(r, s3 + 0xCu, 0x6C0403F5u);
    for (uint32_t k = 0; k < 4; ++k) copy_qword(r, s3 + 0x10u + 0x10u * k, SPAD_3440 + 0x10u * k);
    /* CNT 9: STCYCL 1, 1, UNPACK of 8 rows to 0: D_70003AC0 and the 0x70003400
     * block, each times the scaled matrix. */
    const uint32_t second = tag(r, word(r, CTX_PTR) + 0x1Cu, 0x10u, 0, 9, 0xA0u);
    {
        uint8_t *z = memory(r, second + 0x10u, 16, 1);
        if (z) { memset(z, 0, 16); if (r->h->store) r->h->store(r->h->context, second + 0x10u, 16); }
    }
    store(r, second + 0x14u, 0x01000101u);
    store(r, second + 0x18u, 0);
    store(r, second + 0x1Cu, 0x6C080000u);
    c3(r, 0x001026D0u, second + 0x20u, SPAD_3AC0, sp + 0x60u);
    c3(r, 0x001026D0u, second + 0x60u, SPAD_3400, sp + 0x60u);
    /* The colour, one byte per lane through 00128250 (v0; unmasked ORs of
     * 32-bit shifts, sign-extended, and the last v0 as it is). */
    uint64_t rgba = sx(soft_u(r, word(r, colour + 0xCu)) << 24);
    rgba |= sx(soft_u(r, word(r, colour + 8u)) << 16);
    rgba |= sx(soft_u(r, word(r, colour + 4u)) << 8);
    rgba |= soft_v0(r, word(r, colour));
    c2(r, 0x00102958u, sp + 0xE0u, SPAD_3AC0);                            /* copy_qw4 */
    c3(r, 0x001026D0u, sp + 0x120u, word(r, CTX_PTR) + 0x2380u, sp + 0x60u);
    c3(r, 0x001026D0u, SPAD_3AC0, word(r, CTX_PTR) + 0x2340u, sp + 0x120u);
    {
        const uint64_t v[3] = {3, 2, flag == 0 ? 9u : 5u};
        (void)call(r, 0x001D1F80u, 3, 0, v, NULL);
    }
    c1(r, flag == 0 ? 0x001D4E20u : 0x001D4EB0u, 3);
    {
        const uint64_t v[2] = {3, rgba};
        const uint32_t g[1] = {0};
        (void)call(r, 0x001D7080u, 2, 1, v, g);
    }
    c2(r, 0x001D4F30u, 3, model);
    c2(r, flag == 0 ? 0x001D4B80u : 0x001D4C30u, 3, model);
    c2(r, 0x00102958u, SPAD_3AC0, sp + 0xE0u);
    DONE();
}

/* ------------------------------------------------------------ 001DA290 / 001DA1E0 */

int em_aim_fire_lamp_001DA290(EmAimFireTarget *h, int32_t a0, int32_t a1)
{
    ENTER(0x001DA290u, 0x70u);
    const uint32_t sp = r->sp;
    uint8_t rows[64];
    const uint8_t *src = memory(r, 0x002531D0u, 64, 0);                   /* D_002531D0, four LQs */
    if (!src) return -1;
    memcpy(rows, src, 64);
    uint8_t *dst = memory(r, sp + 0x30u, 64, 1);
    if (!dst) return -1;
    for (unsigned k = 0; k < 4; ++k) {
        memcpy(dst + 16 * k, rows + 16 * k, 16);
        if (r->h->store) r->h->store(r->h->context, sp + 0x30u + 16u * k, 16);
    }
    {
        const uint64_t v[3] = {sx((uint32_t)a0), 2, 9};
        (void)call(r, 0x001D1F80u, 3, 0, v, NULL);
    }
    c3(r, 0x001DA1E0u, sx((uint32_t)a0), sp + 0x30u, sx((uint32_t)a1));
    DONE();
}

int em_aim_fire_lamp_001DA1E0(EmAimFireTarget *h, int32_t a0, uint32_t rows, int32_t a2, uint32_t *result)
{
    ENTER(0x001DA1E0u, 0);
    const uint32_t cw = word(r, CTX_PTR) + ((uint32_t)a0 << 2) + 0x10u;
    const uint32_t t = tag(r, cw, 0x10u, 0, 7, 0x80u);
    {
        uint8_t *z = memory(r, t + 0x10u, 16, 1);
        if (z) { memset(z, 0, 16); if (r->h->store) r->h->store(r->h->context, t + 0x10u, 16); }
    }
    store(r, t + 0x1Cu, 0x50000006u);                                     /* DIRECT 6 */
    put(r, t + 0x20u, UINT64_C(0x8001) | (UINT64_C(0x50224000) << 32), 8);   /* PRE, PRIM 0x044, NLOOP 1 */
    put(r, t + 0x28u, 0x44441u, 8);                                        /* RGBAQ, XYZF2 x 4 */
    store(r, t + 0x30u, 0);
    store(r, t + 0x34u, 0);
    store(r, t + 0x38u, 0);
    store(r, t + 0x3Cu, (uint32_t)a2);
    for (uint32_t k = 0; k < 4; ++k) copy_qword(r, t + 0x40u + 0x10u * k, rows + 0x10u * k);
    if (result) *result = t + 0x10u;
    DONE();
}

/* ------------------------------------------------------------ 001D4E20 .. 001D4C30 */

static int kernel_head(Run *r, int32_t a0, uint32_t kernel, uint32_t templates)
{
    c1(r, 0x001D4750u, sx((uint32_t)a0));                                 /* vif_build_unpack_const */
    c2(r, 0x001D2090u, sx((uint32_t)a0), kernel);                         /* vif_append_ref_tag */
    const uint32_t ctx = word(r, CTX_PTR);
    const uint32_t slot = word(r, ctx + 0x9Cu);
    (void)tag(r, ctx + ((uint32_t)a0 << 2) + 0x10u, 0x30u, templates + (slot << 7), 8, 0x10u);   /* REF */
    return r->h->fault ? -1 : 0;
}

int em_aim_fire_lamp_001D4E20(EmAimFireTarget *h, int32_t a0)
{
    ENTER(0x001D4E20u, 0x20u);
    (void)kernel_head(r, a0, LEVEL_KERNEL, TEMPLATE_FLAT);
    DONE();
}

int em_aim_fire_lamp_001D4EB0(EmAimFireTarget *h, int32_t a0)
{
    ENTER(0x001D4EB0u, 0x20u);
    (void)kernel_head(r, a0, LEVEL_KERNEL, TEMPLATE_GOURAUD);
    DONE();
}

int em_aim_fire_lamp_001D4B80(EmAimFireTarget *h, int32_t a0, uint32_t a1)
{
    ENTER(0x001D4B80u, 0x30u);
    if (kernel_head(r, a0, CLIP_KERNEL, TEMPLATE_FLAT) == 0)
        c2(r, 0x001D4A90u, sx((uint32_t)a0), a1);
    DONE();
}

int em_aim_fire_lamp_001D4C30(EmAimFireTarget *h, int32_t a0, uint32_t a1)
{
    ENTER(0x001D4C30u, 0x30u);
    if (kernel_head(r, a0, CLIP_KERNEL, TEMPLATE_GOURAUD) == 0)
        c2(r, 0x001D4A90u, sx((uint32_t)a0), a1);
    DONE();
}
