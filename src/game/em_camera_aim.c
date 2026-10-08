/* em_camera_aim.c - see em_camera_aim.h and docs/CAMERA_LIVE.md section 7.
 *
 * Every address in a comment is the original instruction translated there.
 * Where the decomp's NEARMISS C differs from the instructions, this file
 * follows the instructions:
 *   - 00197D20 state 3 calls 00197490(cam, player, 1) (00198020);
 *   - 001999C0 passes 001DB800 nothing it reads, and only its D_00810CA7
 *     arms reach the D_00810CA5 == 6 call of 0022E7F0 (00199C10): the
 *     D_00810CA4 arms return before it (00199A20, 00199C30);
 *   - 001999C0's D_00810CA7 == 9 arm calls 001D2830(1, 0) (00199BC4).
 *
 * Float arithmetic: em_ee_float.h (docs/EE_FLOAT_MODEL.md), operands in the
 * instructions' order. */
#include "game/em_camera_aim.h"

#include "game/em_ee_float.h"

#include <stddef.h>

/* Float constants as the instructions build them. */
#define F_0      UINT32_C(0x00000000)
#define F_0_02   UINT32_C(0x3CA3D70A)
#define F_0_2    UINT32_C(0x3E4CCCCD)
#define F_0_4    UINT32_C(0x3ECCCCCD)
#define F_0_6    UINT32_C(0x3F19999A)
#define F_1      UINT32_C(0x3F800000)
#define F_3      UINT32_C(0x40400000)
#define F_4      UINT32_C(0x40800000)
#define F_5      UINT32_C(0x40A00000)
#define F_6      UINT32_C(0x40C00000)
#define F_7      UINT32_C(0x40E00000)
#define F_8      UINT32_C(0x41000000)
#define F_9      UINT32_C(0x41100000)
#define F_11     UINT32_C(0x41300000)
#define F_16     UINT32_C(0x41800000)
#define F_17     UINT32_C(0x41880000)
#define F_18     UINT32_C(0x41900000)
#define F_19     UINT32_C(0x41980000)
#define F_20     UINT32_C(0x41A00000)
#define F_22     UINT32_C(0x41B00000)
#define F_23     UINT32_C(0x41B80000)
#define F_30     UINT32_C(0x41F00000)
#define F_200    UINT32_C(0x43480000)
#define F_473    UINT32_C(0x43EC8000)
#define F_1E6    UINT32_C(0x49742400)
#define F_1E7    UINT32_C(0x4B189680)
#define F_M2     UINT32_C(0xC0000000)
#define F_M6     UINT32_C(0xC0C00000)
#define F_M15    UINT32_C(0xC1700000)
#define F_M22    UINT32_C(0xC1B00000)
#define F_M25    UINT32_C(0xC1C80000)
#define F_M30    UINT32_C(0xC1F00000)

/* Original addresses. */
#define EYE      UINT32_C(0x008105D0)      /* D_008105D0 (x, y +4, z +8) */
#define TGT      UINT32_C(0x008105E0)      /* D_008105E0 */
#define PLAYER   UINT32_C(0x008102B0)      /* D_008102B0 */
#define D_810248 UINT32_C(0x00810248)
#define D_8106C6 UINT32_C(0x008106C6)
#define D_810700 UINT32_C(0x00810700)
#define D_810702 UINT32_C(0x00810702)
#define D_81078B UINT32_C(0x0081078B)
#define D_810CA4 UINT32_C(0x00810CA4)
#define D_810CA5 UINT32_C(0x00810CA5)
#define D_810CA7 UINT32_C(0x00810CA7)
#define D_810E70 UINT32_C(0x00810E70)
#define D_81C040 UINT32_C(0x0081C040)
#define D_2754E8 UINT32_C(0x002754E8)
#define D_2754EC UINT32_C(0x002754EC)
#define D_2754F0 UINT32_C(0x002754F0)
#define S_3040   UINT32_C(0x70003040)
#define S_31B0   UINT32_C(0x700031B0)
#define S_31D0   UINT32_C(0x700031D0)
#define S_31D4   UINT32_C(0x700031D4)
#define S_3400   UINT32_C(0x70003400)
#define S_3430   UINT32_C(0x70003430)
#define S_3600   UINT32_C(0x70003600)
#define S_3610   UINT32_C(0x70003610)
#define S_3630   UINT32_C(0x70003630)
#define S_38A0   UINT32_C(0x700038A0)
#define S_38B0   UINT32_C(0x700038B0)
#define S_38C0   UINT32_C(0x700038C0)
#define S_3A10   UINT32_C(0x70003A10)
#define S_3A20   UINT32_C(0x70003A20)
#define S_3A24   UINT32_C(0x70003A24)
#define S_3B50   UINT32_C(0x70003B50)

/* Callees by original address. */
#define FN_COPY      UINT32_C(0x00102948)   /* 16-byte copy (dst, src) */
#define FN_IDENTITY  UINT32_C(0x001029C0)
#define FN_EULER     UINT32_C(0x00102C58)
#define FN_TRANS     UINT32_C(0x001031E0)
#define FN_APPLY     UINT32_C(0x001026A0)
#define FN_SUB       UINT32_C(0x001028D0)
#define FN_ADD       UINT32_C(0x001028B8)
#define FN_MUL       UINT32_C(0x001028E8)
#define FN_SCALE     UINT32_C(0x00103230)
#define FN_DOT       UINT32_C(0x00102738)
#define FN_NORM      UINT32_C(0x00102760)
#define FN_SCALE2    UINT32_C(0x00102900)
#define FN_FABS      UINT32_C(0x0011DF78)
#define FN_SQRT      UINT32_C(0x0011E748)
#define FN_ATAN2     UINT32_C(0x0011E620)
#define FN_SIN       UINT32_C(0x0011E2A8)
#define FN_COS       UINT32_C(0x0011DE90)
#define FN_WRAP      UINT32_C(0x001B1470)
#define FN_Y_EASE    UINT32_C(0x0018C4B0)
#define FN_XZ_EASE   UINT32_C(0x0018C6A0)
#define FN_Y_CHASE   UINT32_C(0x0018C850)
#define FN_XZ_CHASE  UINT32_C(0x0018C920)
#define FN_SOLVE     UINT32_C(0x0018D7B0)
#define FN_CLAMP     UINT32_C(0x00191210)
#define FN_RELEASE   UINT32_C(0x00197490)
#define FN_SEGMENT   UINT32_C(0x0019A910)
#define FN_MOVE      UINT32_C(0x00183010)
#define FN_LIFT      UINT32_C(0x00182F90)
#define FN_ZOOM      UINT32_C(0x001D2610)
#define FN_2830      UINT32_C(0x001D2830)
#define FN_FOG       UINT32_C(0x0021B9A0)
#define FN_MARKER    UINT32_C(0x0022E7F0)
#define FN_2040      UINT32_C(0x001D2040)
#define FN_DB830     UINT32_C(0x001DB830)
#define FN_DB9D0     UINT32_C(0x001DB9D0)
#define FN_DBCB0     UINT32_C(0x001DBCB0)
#define FN_DC020     UINT32_C(0x001DC020)
#define FN_DC610     UINT32_C(0x001DC610)
#define FN_DC890     UINT32_C(0x001DC890)
#define FN_99770     UINT32_C(0x00199770)
#define FN_MISSILE   UINT32_C(0x001DBE20)   /* the missile-launcher sight */
#define FN_DELTA     UINT32_C(0x001DBF00)   /* the auto-sight */
#define FN_TACTICAL  UINT32_C(0x001DC960)   /* the tactical sight */
#define FN_NIGHT     UINT32_C(0x001DBD50)   /* the night-vision sight */

/* ---- the host ------------------------------------------------------------ */

typedef struct {
    EmCamAim *h;
    uint32_t entry;
} Run;

static void fault(Run *r, int code, uint32_t address)
{
    if (!r->h->fault) {
        r->h->fault = code;
        r->h->fault_function = r->entry;
        r->h->fault_address = address;
    }
}

static int failed(const Run *r) { return r->h->fault != 0; }

static uint8_t *memory(Run *r, uint32_t address, uint32_t size, int write)
{
    if (r->h->fault) return NULL;
    if (!r->h->map) {
        fault(r, 1, address);
        return NULL;
    }
    uint8_t *p = r->h->map(r->h->context, address, size, write);
    if (!p) fault(r, 2, address);
    return p;
}

static uint32_t readn(Run *r, uint32_t a, unsigned n)
{
    const uint8_t *p = memory(r, a, n, 0);
    uint32_t v = 0;
    if (p)
        for (unsigned i = 0; i < n; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static uint32_t w32(Run *r, uint32_t a) { return readn(r, a, 4); }
static uint32_t u8(Run *r, uint32_t a) { return readn(r, a, 1); }
static int32_t s16(Run *r, uint32_t a) { return (int16_t)readn(r, a, 2); }

static void put(Run *r, uint32_t a, uint32_t v, unsigned n)
{
    uint8_t *p = memory(r, a, n, 1);
    if (!p) return;
    for (unsigned i = 0; i < n; ++i) p[i] = (uint8_t)(v >> (8 * i));
    if (r->h->store) r->h->store(r->h->context, a, n);
}

static void st32(Run *r, uint32_t a, uint32_t v) { put(r, a, v, 4); }
static void st8(Run *r, uint32_t a, uint32_t v) { put(r, a, v, 1); }
static void st16(Run *r, uint32_t a, uint32_t v) { put(r, a, v, 2); }

static uint64_t sx(uint32_t v) { return (uint64_t)(int64_t)(int32_t)v; }

/* A callee: integer arguments a0.. (na of them) and float arguments
 * f12.. (nf of them), as the instructions leave them at the jal. */
static EmAimFireTargetCall call(Run *r, uint32_t sp, uint32_t fn, unsigned na, unsigned nf,
                                uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t f0, uint32_t f1)
{
    EmAimFireTargetCall c = {fn, sp, {sx(a0), sx(a1), sx(a2), sx(a3), 0, 0, 0},
                             {f0, f1, 0, 0, 0, 0, 0, 0}, na, nf, 0, 0};
    if (r->h->fault) return c;
    if (!r->h->call) fault(r, 1, fn);
    else if (r->h->call(r->h->context, &c) < 0) fault(r, 3, fn);
    return c;
}

static void c0(Run *r, uint32_t sp, uint32_t fn) { (void)call(r, sp, fn, 0, 0, 0, 0, 0, 0, 0, 0); }
static void c1(Run *r, uint32_t sp, uint32_t fn, uint32_t a) { (void)call(r, sp, fn, 1, 0, a, 0, 0, 0, 0, 0); }
static void c2(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b)
{
    (void)call(r, sp, fn, 2, 0, a, b, 0, 0, 0, 0);
}
static void c3(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{
    (void)call(r, sp, fn, 3, 0, a, b, c, 0, 0, 0);
}
static uint32_t c3v(Run *r, uint32_t sp, uint32_t fn, uint32_t a, uint32_t b, uint32_t c)
{
    return (uint32_t)call(r, sp, fn, 3, 0, a, b, c, 0, 0, 0).v0;
}
/* fn(f12) -> f0 */
static uint32_t f1v(Run *r, uint32_t sp, uint32_t fn, uint32_t x)
{
    return call(r, sp, fn, 0, 1, 0, 0, 0, 0, x, 0).f0;
}
/* 00103230(dst, src, f12) / 00102900(dst, src, f12) */
static void scale(Run *r, uint32_t sp, uint32_t fn, uint32_t dst, uint32_t src, uint32_t f)
{
    (void)call(r, sp, fn, 2, 1, dst, src, 0, 0, f, 0);
}
/* 0018C4B0(vec, f12 target y, f13 step) / 0018C850 */
static uint32_t y_ease(Run *r, uint32_t sp, uint32_t fn, uint32_t vec, uint32_t y, uint32_t step)
{
    return (uint32_t)call(r, sp, fn, 1, 2, vec, 0, 0, 0, y, step).v0;
}
/* 0018C6A0(goal, vec, f12 step) / 0018C920 */
static uint32_t xz_ease(Run *r, uint32_t sp, uint32_t fn, uint32_t goal, uint32_t vec, uint32_t step)
{
    return (uint32_t)call(r, sp, fn, 2, 1, goal, vec, 0, 0, step, 0).v0;
}

#define ADD em_ee_add_bits
#define SUB em_ee_sub_bits
#define MUL em_ee_mul_bits
#define LT em_ee_c_lt_bits
#define LE em_ee_c_le_bits

#define BEGIN(fn) Run run_ = {h, (fn)}, *r = &run_; \
    if (!h || h->fault) return -1
#define END() return failed(r) ? -1 : 0

/* ======================================================================
 * The routines (internal forms share the root's Run; `sp` is the caller's
 * stack pointer, the frame size is each routine's own)
 * ====================================================================== */

static void db800(Run *r)
{
    st8(r, D_81C040 + 3, 0);                                         /* 001DB800 */
    st8(r, D_81C040 + 2, 0);
    st8(r, D_81C040 + 1, 0);
    st8(r, D_81C040 + 0, 0);
}

static uint32_t sight_999C0(Run *r, uint32_t sp, uint32_t pl, int32_t a1)
{
    sp -= 0x50;
    uint32_t zoom = w32(r, pl + 0x20) + 0x1F0;                       /* 001999D8 */
    uint32_t result = 0;
    if (a1 != 0) c2(r, sp, FN_2040, 1, 0);                           /* 001999F4 */
    uint32_t mode = u8(r, D_810CA4);                                 /* 001999FC */
    if (failed(r)) return 0;
    if (mode == 2) {                                                 /* the missile launcher */
        if (a1 == 0) { db800(r); return 0; }                         /* 00199A18 */
        (void)call(r, sp, FN_DB830, 2, 1, 1, 0xFF, 0, 0, F_1, 0);
        (void)call(r, sp, FN_MISSILE, 1, 1, 1, 0, 0, 0, F_1, 0);
        c1(r, sp, FN_99770, pl);
        return 0;
    }
    if (mode == 1) {                                                 /* 00199A60 */
        if (a1 == 0) { db800(r); return 0; }
        (void)call(r, sp, FN_DB9D0, 2, 1, 1, 2, 0, 0, F_1, 0);
        (void)call(r, sp, FN_DC610, 1, 1, 1, 0, 0, 0, F_1, 0);
        uint32_t z = w32(r, zoom + 0x24);
        (void)call(r, sp, FN_DC890, 1, 1, 1, 0, 0, 0, z, 0);
        return 0;
    }
    if (mode == 0) {                                                 /* 00199AB4 */
        if (a1 == 0) { db800(r); return 0; }
        (void)call(r, sp, FN_DB9D0, 2, 1, 1, 2, 0, 0, F_1, 0);
        (void)call(r, sp, FN_DELTA, 1, 1, 1, 0, 0, 0, F_1, 0);
        uint32_t z = w32(r, zoom + 0x24);
        (void)call(r, sp, FN_DC020, 1, 1, 1, 0, 0, 0, z, 0);
        return 0;
    }
    uint32_t kind = u8(r, D_810CA7);                                 /* 00199B08 */
    if (failed(r)) return 0;
    if (kind == 8) {
        if (a1 == 0) {
            db800(r);                                                /* 00199B24 */
        } else {
            (void)call(r, sp, FN_DB9D0, 2, 1, 1, 2, 0, 0, F_1, 0);
            (void)call(r, sp, FN_TACTICAL, 1, 1, 1, 0, 0, 0, F_1, 0);
            uint32_t v = w32(r, D_810248);
            (void)call(r, sp, FN_DBCB0, 1, 1, 1, 0, 0, 0, v, 0);
            v = w32(r, D_810248);
            (void)f1v(r, sp, FN_ZOOM, v);                            /* 00199B68 */
            result = 1;
        }
    } else if (kind == 9) {                                          /* 00199B7C */
        if (a1 == 0) {
            db800(r);
            (void)call(r, sp, FN_FOG, 1, 2, 4, 0, 0, 0, F_0, F_1E6); /* 00199BA4 */
            (void)call(r, sp, FN_FOG, 1, 2, 5, 0, 0, 0, F_0, F_1E7);
            c2(r, sp, FN_2830, 1, 0);                                /* 00199BC8 */
        } else {
            (void)call(r, sp, FN_DB830, 2, 1, 1, 0xFF, 0, 0, F_1, 0);
            (void)call(r, sp, FN_NIGHT, 1, 1, 1, 0, 0, 0, F_1, 0);
        }
    } else if (a1 == 0) {
        db800(r);                                                    /* 00199C08 */
    }
    if (failed(r)) return 0;
    if (u8(r, D_810CA5) == 6) c1(r, sp, FN_MARKER, PLAYER);          /* 00199C10 */
    return result;
}

static uint32_t wall_98240(Run *r, uint32_t sp, uint32_t pl, uint32_t gun)
{
    sp -= 0x30;
    c3(r, sp, FN_SUB, S_38B0, gun + 0xB0, gun + 0xA0);               /* 00198264 */
    st32(r, S_38B0 + 4, 0);                                          /* 00198284 */
    uint32_t d = call(r, sp, FN_DOT, 2, 0, S_38B0, S_38B0, 0, 0, 0, 0).f0;
    st32(r, S_3A20, d);                                              /* 001982A0 */
    if (failed(r)) return 0;
    if (LT(d, F_9)) {
        c2(r, sp, FN_NORM, S_38B0, S_38B0);                          /* 001982B0 */
        scale(r, sp, FN_SCALE2, S_38B0, S_38B0, F_3);                /* 001982CC */
        c3(r, sp, FN_ADD, S_38B0, gun + 0xA0, S_38B0);               /* 001982E4 */
    } else {
        c2(r, sp, FN_COPY, S_38B0, gun + 0xB0);                      /* 001982FC */
    }
    uint32_t hit = c3v(r, sp, FN_SEGMENT, gun + 0xA0, S_38B0, 7);    /* 00198310 */
    if (failed(r) || hit == 0) return 0;
    if (hit != 1) {
        uint32_t node = w32(r, S_31D0);                              /* 00198330 */
        if ((s16(r, node + 0x1A) & 0x2000) == 0) return 0;
    }
    uint32_t who = w32(r, S_31D4);                                   /* 00198348 */
    if (failed(r)) return 0;
    if (who != 0 && u8(r, who + 3) == 0x54) return 1;                /* 00198354 */
    c3(r, sp, FN_SUB, S_38B0, gun + 0xB0, S_31B0);                   /* 0019837C */
    uint32_t x = f1v(r, sp, FN_FABS, w32(r, S_38B0));                /* 00198388 */
    st32(r, S_38B0, x);
    st32(r, S_38B0 + 4, 0);
    uint32_t z = f1v(r, sp, FN_FABS, w32(r, S_38B0 + 8));            /* 001983A4 */
    st32(r, S_38B0 + 8, z);
    st32(r, S_38B0 + 0xC, 0);
    uint32_t node = w32(r, S_31D0);                                  /* 001983C0 */
    st32(r, S_38C0, w32(r, node + 0x24));
    st32(r, S_38C0 + 4, w32(r, node + 0x28));
    st32(r, S_38C0 + 8, w32(r, node + 0x2C));
    st32(r, S_38C0 + 0xC, F_1);                                      /* 0019840C */
    (void)call(r, sp, FN_MUL, 4, 0, S_38A0, S_38B0, S_38C0, node, 0, 0);
    c2(r, sp, FN_MOVE, pl, S_38A0);                                  /* 00198418 */
    return 1;
}

static void hold_98440(Run *r, uint32_t sp, uint32_t cam, uint32_t pl, int32_t a2)
{
    sp -= 0x60;
    uint32_t e8 = w32(r, D_2754E8);                                  /* 0019845C */
    uint32_t gun = w32(r, pl + 0x20);
    uint32_t f0 = w32(r, D_2754F0);
    uint32_t ec = w32(r, D_2754EC);
    st32(r, S_3600, em_ee_neg_bits(e8));                             /* 00198478 */
    st32(r, S_3600 + 4, f0);
    st32(r, S_3600 + 8, ec);
    st32(r, S_3600 + 0xC, 0);
    uint32_t bone = w32(r, gun + 0x110);                             /* 00198494 */
    if (failed(r)) return;
    c3(r, sp, FN_APPLY, S_3610, bone + 0x90, S_3600);                /* 001984B4 */
    for (unsigned k = 0; k < 12; k += 4)                             /* 001984BC */
        st32(r, cam + 0x10 + k, ADD(w32(r, gun + 0xA0 + k), w32(r, S_3610 + k)));
    if (a2 == 0) {
        (void)y_ease(r, sp, FN_Y_CHASE, EYE, w32(r, cam + 0x14), F_0_4);    /* 00198510 */
        (void)xz_ease(r, sp, FN_XZ_CHASE, cam + 0x10, EYE, F_0_4);          /* 0019852C */
    } else {
        c2(r, sp, FN_COPY, EYE, cam + 0x10);                                /* 00198544 */
    }
    uint32_t dot = w32(r, gun + 0x1F0 + 0x20);                              /* 00198550 */
    if (failed(r)) return;
    if (dot != 0 && u8(r, D_8106C6) != 0) {
        c2(r, sp, FN_COPY, cam + 0x20, gun + 0x1F0 + 0x10);                 /* 0019856C */
    } else {
        scale(r, sp, FN_SCALE, S_3610, gun + 0xC0, F_200);                  /* 0019858C */
        for (unsigned k = 0; k < 12; k += 4)
            st32(r, cam + 0x20 + k, ADD(w32(r, gun + 0xA0 + k), w32(r, S_3610 + k)));
    }
    c2(r, sp, FN_COPY, TGT, cam + 0x20);                                    /* 001985D8 */
    if (failed(r)) return;
    uint32_t pushed = wall_98240(r, sp, pl, gun);                           /* 001985E4 */
    if (failed(r)) return;
    if (a2 == 2 && pushed != 0) {
        c3(r, sp, FN_ADD, cam + 0x10, cam + 0x10, S_38A0);                  /* 0019860C */
        c2(r, sp, FN_COPY, EYE, cam + 0x10);                                /* 0019861C */
    }
}

static uint32_t draw_98050(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x50;
    uint32_t gun = w32(r, pl + 0x20);                                       /* 00198068 */
    uint32_t flags = 0;
    c2(r, sp, FN_COPY, cam + 0x30, S_3B50);                                 /* 00198080 */
    c1(r, sp, FN_IDENTITY, S_3400);
    c3(r, sp, FN_EULER, S_3400, S_3400, cam + 0x30);
    uint32_t s = f1v(r, sp, FN_SIN, w32(r, pl + 0xC4));                     /* 001980AC */
    st32(r, cam + 0x10, ADD(w32(r, pl + 0xA0), MUL(F_M2, s)));
    st32(r, cam + 0x14, ADD(F_6, w32(r, pl + 0xB4)));                       /* 001980DC */
    uint32_t c = f1v(r, sp, FN_COS, w32(r, pl + 0xC4));                     /* 001980E4 */
    st32(r, cam + 0x18, ADD(w32(r, pl + 0xA8), MUL(F_M2, c)));
    flags |= y_ease(r, sp, FN_Y_CHASE, EYE, w32(r, cam + 0x14), F_0_2);     /* 00198118 */
    flags |= xz_ease(r, sp, FN_XZ_CHASE, cam + 0x10, EYE, F_0_2);           /* 00198138 */
    st32(r, S_3600, 0);                                                     /* 00198144 */
    st32(r, S_3600 + 4, 0);
    st32(r, S_3600 + 8, F_20);
    flags <<= 4;
    st32(r, S_3600 + 0xC, 0);                                               /* 00198180 */
    c3(r, sp, FN_APPLY, cam + 0x20, S_3400, S_3600);
    for (unsigned k = 0; k < 12; k += 4)                                    /* 00198184 */
        st32(r, cam + 0x20 + k, ADD(w32(r, cam + 0x20 + k), w32(r, cam + 0x10 + k)));
    flags |= y_ease(r, sp, FN_Y_CHASE, TGT, w32(r, cam + 0x24), F_0_2);     /* 001981C8 */
    flags |= xz_ease(r, sp, FN_XZ_CHASE, cam + 0x20, TGT, F_0_2);           /* 001981E8 */
    st16(r, cam + 8, (uint32_t)(s16(r, cam + 8) + 1));                      /* 001981F4 */
    if (failed(r)) return 0;
    if (s16(r, cam + 8) >= 0xF) (void)wall_98240(r, sp, pl, gun);           /* 00198204 */
    return flags;
}

static void hold_97870(Run *r, uint32_t sp, uint32_t cam, uint32_t pl, int32_t a2)
{
    sp -= 0x50;
    uint32_t code = w32(r, pl + 0x230);                                     /* 00197888 */
    uint32_t gun = w32(r, pl + 0x20);
    if (failed(r)) return;
    if (code == 0x2A) c2(r, sp, FN_COPY, S_3A10, S_3040);                   /* 001978AC */
    else c2(r, sp, FN_COPY, S_3A10, pl + 0xA0);                             /* 001978C4 */
    scale(r, sp, FN_SCALE, cam + 0x20, gun + 0xC0, F_16);                   /* 001978D8 */
    st32(r, cam + 0x20, ADD(w32(r, cam + 0x20), w32(r, S_3A10)));          /* 00197900 */
    st32(r, cam + 0x24, ADD(w32(r, cam + 0x24), ADD(F_19, w32(r, S_3A10 + 4))));
    st32(r, cam + 0x28, ADD(w32(r, cam + 0x28), w32(r, S_3A10 + 8)));
    st32(r, cam + 0x2C, F_1);                                               /* 00197938 */
    c1(r, sp, FN_IDENTITY, S_3400);
    c3(r, sp, FN_EULER, S_3400, S_3400, S_3B50);                            /* 00197950 */
    st32(r, S_3600, 0);
    st32(r, S_3600 + 4, 0);
    st32(r, S_3600 + 8, F_M30);
    st32(r, S_3600 + 0xC, F_1);
    c2(r, sp, FN_TRANS, S_3430, S_3A10);                                    /* 0019798C */
    c3(r, sp, FN_APPLY, cam + 0x10, S_3400, S_3600);                        /* 001979A4 */
    scale(r, sp, FN_SCALE, S_3600, gun + 0xC0, F_M30);                      /* 001979BC */
    st32(r, cam + 0x14, ADD(F_19, ADD(w32(r, S_3600 + 4), w32(r, S_3A10 + 4))));   /* 001979E4 */
    uint32_t y = w32(r, S_3600 + 4);                                        /* 001979F4 */
    uint32_t lift;
    if (LT(y, F_M22)) {
        if (LT(y, F_M25)) st32(r, S_3600 + 4, F_M25);                       /* 00197A28 */
        lift = ADD(F_22, w32(r, S_3600 + 4));                               /* 00197A40 */
    } else {
        lift = F_0;                                                         /* 00197A44 */
    }
    uint32_t base = w32(r, S_3A10 + 4);                                     /* 00197A4C */
    uint32_t ceiling = ADD(F_30, base);
    uint32_t eye_y = w32(r, cam + 0x14);
    if (!LE(eye_y, ceiling)) {
        st32(r, cam + 0x14, ceiling);                                       /* 00197A74 */
    } else if (s16(r, cam + 0x5A) & 0x10) {
        uint32_t floor = ADD(F_11, base);                                   /* 00197A94 */
        if (LT(eye_y, floor)) st32(r, cam + 0x14, floor);                   /* 00197AAC */
    } else {
        uint32_t floor = ADD(0x40000000u, base);                            /* 00197ABC */
        if (LT(eye_y, floor)) st32(r, cam + 0x14, floor);                   /* 00197AD0 */
    }
    if (a2 == 0) {
        (void)y_ease(r, sp, FN_Y_EASE, TGT, w32(r, cam + 0x24), F_0_6);     /* 00197AF0 */
        (void)xz_ease(r, sp, FN_XZ_EASE, cam + 0x20, TGT, F_0_6);           /* 00197B0C */
    } else {
        c2(r, sp, FN_COPY, TGT, cam + 0x20);                                /* 00197B24 */
    }
    (void)call(r, sp, FN_SOLVE, 2, 0, cam, 2, 0, 0, 0, 0);                  /* 00197B30 */
    int done = 0;
    if (a2 != 0) {
        c2(r, sp, FN_COPY, EYE, cam + 0x10);                                /* 00197B48 */
        done = 1;
    }
    c3(r, sp, FN_SUB, S_3630, cam + 0x10, S_3A10);                          /* 00197B6C */
    uint32_t dx = w32(r, S_3630), dz = w32(r, S_3630 + 8);                  /* 00197B78 */
    uint32_t d = f1v(r, sp, FN_SQRT, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
    if (failed(r)) return;
    if (LT(d, F_7)) {                                                       /* 00197B9C */
        uint32_t near = ADD(ADD(F_18, w32(r, S_3A10 + 4)), lift);           /* 00197BC0 */
        if (LE(w32(r, cam + 0x54), near)) {                                 /* 00197BC8 */
            if (LT(w32(r, cam + 0x14), near)) {                             /* 00197BF8 */
                c1(r, sp, FN_IDENTITY, S_3400);                             /* 00197C0C */
                c3(r, sp, FN_EULER, S_3400, S_3400, S_3B50);
                st32(r, S_3600, 0);
                st32(r, S_3600 + 4, 0);
                st32(r, S_3600 + 8, F_5);
                st32(r, S_3600 + 0xC, F_1);
                c2(r, sp, FN_TRANS, S_3430, S_3A10);                        /* 00197C64 */
                c3(r, sp, FN_APPLY, cam + 0x10, S_3400, S_3600);            /* 00197C7C */
                st32(r, cam + 0x14, ADD(ADD(F_18, w32(r, S_3A10 + 4)), lift));   /* 00197CA8 */
                (void)call(r, sp, FN_SOLVE, 2, 0, cam, 2, 0, 0, 0, 0);
                c2(r, sp, FN_COPY, EYE, cam + 0x10);                        /* 00197CB4 */
                done = 1;
            }
        } else if (LT(w32(r, cam + 0x14), near)) {                          /* 00197BDC */
            st32(r, cam + 0x14, near);                                      /* 00197BF0 */
        }
    }
    if (!done) {
        (void)xz_ease(r, sp, FN_XZ_EASE, cam + 0x10, EYE, F_4);             /* 00197CD8 */
        (void)y_ease(r, sp, FN_Y_EASE, EYE, w32(r, cam + 0x14), F_4);       /* 00197CF0 */
    }
}

static void draw_97740(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x30;
    c2(r, sp, FN_COPY, cam + 0x30, S_3B50);                                 /* 00197760 */
    c1(r, sp, FN_IDENTITY, S_3400);
    c3(r, sp, FN_EULER, S_3400, S_3400, cam + 0x30);                        /* 00197784 */
    st32(r, S_3600, 0);
    st32(r, S_3600 + 4, F_19);
    st32(r, S_3600 + 8, F_6);
    st32(r, S_3600 + 0xC, F_1);
    c2(r, sp, FN_TRANS, S_3430, pl + 0xA0);                                 /* 001977C0 */
    c3(r, sp, FN_APPLY, cam + 0x20, S_3400, S_3600);                        /* 001977D8 */
    st32(r, S_3600, 0);
    st32(r, S_3600 + 4, F_19);
    st32(r, S_3600 + 8, F_M30);
    st32(r, S_3600 + 0xC, F_1);
    c3(r, sp, FN_APPLY, cam + 0x10, S_3400, S_3600);                        /* 0019781C */
    (void)y_ease(r, sp, FN_Y_EASE, TGT, w32(r, cam + 0x24), F_0_4);         /* 00197838 */
    (void)xz_ease(r, sp, FN_XZ_EASE, cam + 0x20, TGT, F_0_4);               /* 00197854 */
}

static void lift_912B0(Run *r, uint32_t sp, uint32_t pl)
{
    sp -= 0x20;
    if (u8(r, D_810700) != 0x10) return;                                    /* 001912C4 */
    if (u8(r, D_810702) != 0) return;
    if (u8(r, D_81078B) == 0xFF) return;
    if (failed(r) || !LT(w32(r, pl + 0xA8), F_473)) return;                 /* 0019130C */
    st32(r, pl + 0xA8, F_473);                                              /* 0019132C */
    c2(r, sp, FN_COPY, S_3600, pl + 0xA0);
    c2(r, sp, FN_LIFT, pl, S_3600);                                         /* 00191338 */
}

static void action1_97D20(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x40;
    uint32_t st = u8(r, cam + 1);                                           /* 00197D38 */
    if (failed(r)) return;
    switch (st) {
    case 3:
        c3(r, sp, FN_RELEASE, cam, pl, 1);                                  /* 00198020 */
        return;
    case 0:
        st8(r, cam + 1, st + 1);                                            /* 00197D84 */
        st8(r, cam + 2, 0);
        st8(r, cam + 0x8B, 0);                                              /* 00197D9C */
        c2(r, sp, FN_COPY, S_3040, pl + 0xA0);
        /* fallthrough */
    case 1: {
        uint32_t code = w32(r, pl + 0x230);                                 /* 00197DA0 */
        if (failed(r)) return;
        if (code != 0xD && code != 0x2A) {
            st8(r, cam + 1, 3);                                             /* 00197DC4 */
        } else {
            if (u8(r, pl + 0x1F1) == 1) st8(r, cam + 1, u8(r, cam + 1) + 1);   /* 00197DE0 */
            draw_97740(r, sp, cam, pl);                                     /* 00197DE8 */
        }
        (void)call(r, sp, FN_SOLVE, 2, 0, cam, 2, 0, 0, 0, 0);              /* 00197DF4 */
        (void)xz_ease(r, sp, FN_XZ_EASE, cam + 0x10, EYE, F_4);             /* 00197E0C */
        (void)y_ease(r, sp, FN_Y_EASE, EYE, w32(r, cam + 0x14), F_4);       /* 00197E24 */
        uint32_t top = ADD(F_23, w32(r, pl + 0xA4));                        /* 00197E40 */
        if (failed(r)) return;
        if (LT(w32(r, EYE + 4), top)) {                                     /* 00197E44 */
            uint32_t dx = SUB(w32(r, EYE), w32(r, pl + 0xA0));              /* 00197E6C */
            uint32_t dz = SUB(w32(r, EYE + 8), w32(r, pl + 0xA8));
            uint32_t d = f1v(r, sp, FN_SQRT, em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz));
            st32(r, S_3A20, d);                                             /* 00197E98 */
            if (failed(r)) return;
            if (LT(d, F_8)) {
                uint32_t a = call(r, sp, FN_ATAN2, 0, 2, 0, 0, 0, 0, dx, dz).f0;   /* 00197EA0 */
                a = f1v(r, sp, FN_WRAP, a);                                 /* 00197EA8 */
                st32(r, S_3A24, a);
                uint32_t s = f1v(r, sp, FN_SIN, a);                         /* 00197EB8 */
                uint32_t x = ADD(w32(r, pl + 0xA0), MUL(F_8, s));
                uint32_t again = w32(r, S_3A24);                            /* 00197ED4 */
                st32(r, EYE, x);                                            /* 00197EE4 */
                uint32_t c = f1v(r, sp, FN_COS, again);
                st32(r, EYE + 8, ADD(w32(r, pl + 0xA8), MUL(F_8, c)));      /* 00197F00 */
            }
        }
        c0(r, sp, FN_CLAMP);                                                /* 00197F04 */
        return;
    }
    case 4:
        st32(r, cam + 0x68, 0);                                             /* 00197F1C */
        (void)f1v(r, sp, FN_ZOOM, F_0);
        if (u8(r, D_810CA7) == 9) {                                         /* 00197F24 */
            (void)call(r, sp, FN_FOG, 1, 2, 0, 0, 0, 0, F_0, F_0);          /* 00197F3C */
            c2(r, sp, FN_2830, 1, 1);
        }
        /* fallthrough */
    case 2: {
        uint32_t code = w32(r, pl + 0x230);                                 /* 00197F50 */
        if (failed(r)) return;
        if (code == 0xD || code == 0x2A) {
            hold_97870(r, sp, cam, pl, 0);                                  /* 00197FEC */
            if (failed(r)) return;
            if (u8(r, D_810CA5) == 6) c1(r, sp, FN_MARKER, pl);             /* 00197FF4 */
        } else if (code == 0xC || code == 0x29) {
            if (u8(r, cam + 0x8B) == 0) c2(r, sp, FN_COPY, pl + 0xA0, S_3040);  /* 00197F84 */
            st8(r, cam + 6, 2);                                             /* 00197FA4 */
            st8(r, cam + 1, 2);
            (void)sight_999C0(r, sp, pl, 0);                                /* 00197FB0 */
            hold_98440(r, sp, cam, pl, 2);                                  /* 00197FC0 */
            lift_912B0(r, sp, pl);                                          /* 00197FC8 */
        } else {
            st8(r, cam + 1, 3);                                             /* 00197FDC */
        }
        c0(r, sp, FN_CLAMP);                                                /* 00198010 */
        return;
    }
    default:
        return;
    }
}

static void action2_98650(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x30;
    uint32_t st = u8(r, cam + 1);                                           /* 00198664 */
    if (failed(r)) return;
    switch (st) {
    case 4:
        c3(r, sp, FN_RELEASE, cam, pl, 0);                                  /* 00198908 */
        return;
    case 3:
        st8(r, cam + 1, st + 1);                                            /* 001988D4 */
        st32(r, cam + 0x68, 0);                                             /* 001988DC */
        (void)f1v(r, sp, FN_ZOOM, F_0);
        if (u8(r, D_810CA7) == 9) c2(r, sp, FN_2830, 1, 1);                 /* 001988F8 */
        c3(r, sp, FN_RELEASE, cam, pl, 0);                                  /* 00198908 */
        return;
    case 0:
        st16(r, cam + 8, 0);                                                /* 001986A8 */
        st8(r, cam + 1, u8(r, cam + 1) + 1);
        st8(r, cam + 2, 0);
        st32(r, cam + 0x68, 0);                                             /* 001986C4 */
        lift_912B0(r, sp, pl);
        st8(r, cam + 0x8B, 0);                                              /* 001986D8 */
        c2(r, sp, FN_COPY, S_3040, pl + 0xA0);
        /* fallthrough */
    case 1: {
        uint32_t code = w32(r, pl + 0x230);                                 /* 001986DC */
        if (failed(r)) return;
        if (code != 0xC && code != 0x29) {
            st8(r, cam + 1, 3);                                             /* 00198700 */
            return;
        }
        (void)draw_98050(r, sp, cam, pl);                                   /* 00198708 */
        if (failed(r) || u8(r, pl + 0x1F1) != 1) return;                    /* 00198710 */
        st8(r, cam + 1, u8(r, cam + 1) + 1);                                /* 00198734 */
        (void)sight_999C0(r, sp, pl, 0);
        hold_98440(r, sp, cam, pl, 1);                                      /* 00198740 */
        return;
    }
    case 2: {
        uint32_t code = w32(r, pl + 0x230);                                 /* 00198750 */
        if (failed(r)) return;
        if (code == 0xC || code == 0x29) {
            hold_98440(r, sp, cam, pl, 0);                                  /* 00198820 */
            uint32_t on = sight_999C0(r, sp, pl, 1);                        /* 0019882C */
            if (failed(r) || on != 1) return;
            uint32_t held = readn(r, D_810E70, 2);                          /* 00198844 */
            if (held & 4) {
                uint32_t f = ADD(w32(r, cam + 0x68), F_0_02);               /* 00198868 */
                st32(r, cam + 0x68, f);                                     /* 00198880 */
                if (!LE(f, F_1)) st32(r, cam + 0x68, F_1);                  /* 00198888 */
            } else if (held & 1) {
                uint32_t f = SUB(w32(r, cam + 0x68), F_0_02);               /* 001988B0 */
                st32(r, cam + 0x68, f);                                     /* 001988C0 */
                if (LT(f, F_0)) st32(r, cam + 0x68, 0);                     /* 001988C8 */
            }
            return;
        }
        (void)sight_999C0(r, sp, pl, 1);                                    /* 00198770 */
        code = w32(r, pl + 0x230);                                          /* 00198778 */
        if (failed(r)) return;
        if (code == 0xD || code == 0x2A) {
            if (u8(r, cam + 0x8B) == 0) c2(r, sp, FN_COPY, pl + 0xA0, S_3040);  /* 001987A8 */
            st8(r, cam + 6, 1);                                             /* 001987B8 */
            st8(r, cam + 1, 4);
            hold_97870(r, sp, cam, pl, 1);                                  /* 001987C4 */
            return;
        }
        st32(r, cam + 0x68, 0);                                             /* 001987DC */
        (void)f1v(r, sp, FN_ZOOM, F_0);
        if (u8(r, D_810CA7) == 9) c2(r, sp, FN_2830, 1, 1);                 /* 001987F8 */
        c3(r, sp, FN_RELEASE, cam, pl, 0);                                  /* 00198808 */
        return;
    }
    default:
        return;
    }
}

static void action5_8CA90(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x30;
    c2(r, sp, FN_COPY, cam + 0x30, S_3B50);                                 /* 0018CAB0 */
    c2(r, sp, FN_COPY, cam + 0x20, pl + 0xA0);                              /* 0018CABC */
    st32(r, cam + 0x24, ADD(w32(r, cam + 0x24), F_17));                     /* 0018CAE4 */
    c2(r, sp, FN_COPY, TGT, cam + 0x20);
    c1(r, sp, FN_IDENTITY, S_3400);                                         /* 0018CAEC */
    c3(r, sp, FN_EULER, S_3400, S_3400, cam + 0x30);                        /* 0018CB04 */
    st32(r, S_3600, 0);
    st32(r, S_3600 + 4, 0);
    st32(r, S_3600 + 8, w32(r, cam + 0xC));                                 /* 0018CB38 */
    st32(r, S_3600 + 0xC, F_1);                                             /* 0018CB4C */
    c3(r, sp, FN_APPLY, EYE, S_3400, S_3600);
    st32(r, EYE, ADD(w32(r, EYE), w32(r, TGT)));                            /* 0018CB6C */
    st32(r, EYE + 4, ADD(w32(r, EYE + 4), ADD(w32(r, TGT + 4), w32(r, cam + 0x5C))));
    st32(r, EYE + 8, ADD(w32(r, EYE + 8), w32(r, TGT + 8)));                /* 0018CBB8 */
    c2(r, sp, FN_COPY, cam + 0x10, EYE);                                    /* 0018CBB4 */
}

/* ======================================================================
 * Camera action 14 (player code 0x28 through the event router 00193EB0)
 * and action 11's raised target 00191530 (docs/CAMERA_LIVE.md section 6;
 * actions 9 and 11 themselves are em_area00_low's 00198CE0 / 00198F10)
 * ====================================================================== */

/* 00198930: action 14's eye 6 behind the player's heading +C4 and 6 above
 * +B4, its target 20 ahead along the published rotation 0x70003B50; both
 * eased into D_008105D0 / D_008105E0 at 0.02. Returns the four eases' bits
 * (the caller ignores them). */
static uint32_t follow_98930(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x40;
    uint32_t res = 0;
    c2(r, sp, FN_COPY, cam + 0x30, S_3B50);                                /* 00198958 */
    c1(r, sp, FN_IDENTITY, S_3400);                                        /* 00198964 */
    c3(r, sp, FN_EULER, S_3400, S_3400, cam + 0x30);                       /* 0019897C */
    uint32_t s = f1v(r, sp, FN_SIN, w32(r, pl + 0xC4));                    /* 00198984 */
    st32(r, cam + 0x10, ADD(w32(r, pl + 0xA0), MUL(F_M6, s)));             /* 001989A4 */
    st32(r, cam + 0x14, ADD(F_6, w32(r, pl + 0xB4)));                      /* 001989B8 */
    uint32_t c = f1v(r, sp, FN_COS, w32(r, pl + 0xC4));                    /* 001989BC */
    st32(r, cam + 0x18, ADD(w32(r, pl + 0xA8), MUL(F_M6, c)));             /* 001989E4 */
    res |= y_ease(r, sp, FN_Y_EASE, EYE, w32(r, cam + 0x14), F_0_02);      /* 001989F0 */
    res |= xz_ease(r, sp, FN_XZ_EASE, cam + 0x10, EYE, F_0_02);            /* 00198A10 */
    st32(r, S_3600, 0);                                                    /* 00198A1C */
    st32(r, S_3600 + 4, 0);
    st32(r, S_3600 + 8, F_20);
    res <<= 4;
    st32(r, S_3600 + 0xC, 0);
    c3(r, sp, FN_APPLY, cam + 0x20, S_3400, S_3600);                       /* 00198A54 */
    st32(r, cam + 0x20, ADD(w32(r, cam + 0x20), w32(r, cam + 0x10)));      /* 00198A78 */
    st32(r, cam + 0x24, ADD(w32(r, cam + 0x24), w32(r, cam + 0x14)));
    st32(r, cam + 0x28, ADD(w32(r, cam + 0x28), w32(r, cam + 0x18)));
    res |= y_ease(r, sp, FN_Y_EASE, TGT, w32(r, cam + 0x24), F_0_02);      /* 00198AA0 */
    res |= xz_ease(r, sp, FN_XZ_EASE, cam + 0x20, TGT, F_0_02);            /* 00198AC0 */
    return res;
}

/* 00198AF0: camera action 14. Sub-states (+1) 0 / 1 follow while the
 * player code is 0x28 and advance on +1F1 == 1; 2 waits; 3 seats the camera
 * behind the player and hands back to action 0. */
static void action14_98AF0(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x30;
    uint32_t st = u8(r, cam + 1);                                          /* 00198B04 */
    if (failed(r)) return;
    switch (st) {
    case 0:
        st8(r, cam + 1, st + 1);                                           /* 00198B40 */
        st8(r, cam + 2, 0);
        /* fall through */
    case 1:
        if (w32(r, pl + 0x230) != 0x28) {                                  /* 00198B48 */
            st8(r, cam + 1, 3);                                            /* 00198B60 */
            return;
        }
        (void)follow_98930(r, sp, cam, pl);                                /* 00198B68 */
        if (u8(r, pl + 0x1F1) == 1) st8(r, cam + 1, u8(r, cam + 1) + 1);   /* 00198B70..8C */
        return;
    case 2:
        /* +1 = 3 when the code is not 0x28 or +1F1 is 2 (the instructions;
         * the decomp's NEARMISS C reads an `and` here). */
        if (w32(r, pl + 0x230) != 0x28 || u8(r, pl + 0x1F1) == 2)          /* 00198B90..A4 */
            st8(r, cam + 1, 3);                                            /* 00198BB4 */
        return;
    case 3:
        c2(r, sp, FN_COPY, cam + 0x30, S_3B50);                            /* 00198BC0 */
        c2(r, sp, FN_COPY, cam + 0x20, pl + 0xA0);                         /* 00198BCC */
        st32(r, cam + 0x24, ADD(w32(r, pl + 0xB4), w32(r, cam + 0x8C)));   /* 00198BEC */
        c1(r, sp, FN_IDENTITY, S_3400);                                    /* 00198BE8 */
        c3(r, sp, FN_EULER, S_3400, S_3400, cam + 0x30);                   /* 00198C00 */
        st32(r, S_3600, 0);                                                /* 00198C0C */
        st32(r, S_3600 + 4, 0);
        st32(r, S_3600 + 8, F_M15);
        st32(r, S_3600 + 0xC, F_1);
        c3(r, sp, FN_APPLY, cam + 0x10, S_3400, S_3600);                   /* 00198C40 */
        st32(r, cam + 0x10, ADD(w32(r, cam + 0x10), w32(r, cam + 0x20)));  /* 00198C60 */
        st32(r, cam + 0x14, ADD(w32(r, cam + 0x14), ADD(w32(r, cam + 0x24), w32(r, cam + 0x5C))));
        st32(r, cam + 0x18, ADD(w32(r, cam + 0x18), w32(r, cam + 0x28)));  /* 00198C8C */
        c2(r, sp, FN_COPY, TGT, cam + 0x20);                               /* 00198C88 */
        c2(r, sp, FN_COPY, EYE, cam + 0x10);                               /* 00198C98 */
        st8(r, cam + 6, 0);                                                /* 00198CA0 */
        if (u8(r, cam + 5) == 0) st8(r, cam + 1, 3);                       /* 00198CA4..B8 */
        else st8(r, cam + 1, 0);
        st8(r, cam + 2, 0);                                                /* 00198CBC */
        st8(r, cam + 3, 0);
        st16(r, cam + 8, 0);
        return;
    default:
        return;
    }
}

/* 00191530: the target at the player's +A0 raised by 17, copied to
 * D_008105E0. */
static void raise_91530(Run *r, uint32_t sp, uint32_t cam, uint32_t pl)
{
    sp -= 0x20;
    c2(r, sp, FN_COPY, cam + 0x20, pl + 0xA0);                             /* 00191544 */
    st32(r, cam + 0x24, ADD(w32(r, cam + 0x24), F_17));                    /* 0019156C */
    c2(r, sp, FN_COPY, TGT, cam + 0x20);                                   /* 00191568 */
}

/* ======================================================================
 * Entries
 * ====================================================================== */

int em_cam_aim_00197D20(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x00197D20u);
    action1_97D20(r, h->sp, cam, player);
    END();
}

int em_cam_aim_00198650(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x00198650u);
    action2_98650(r, h->sp, cam, player);
    END();
}

int em_cam_aim_0018CA90(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x0018CA90u);
    action5_8CA90(r, h->sp, cam, player);
    END();
}

int em_cam_aim_00197740(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x00197740u);
    draw_97740(r, h->sp, cam, player);
    END();
}

int em_cam_aim_00197870(EmCamAim *h, uint32_t cam, uint32_t player, int32_t a2)
{
    BEGIN(0x00197870u);
    hold_97870(r, h->sp, cam, player, a2);
    END();
}

int em_cam_aim_00198050(EmCamAim *h, uint32_t cam, uint32_t player, int32_t *result)
{
    BEGIN(0x00198050u);
    if (!result) { fault(r, 1, 0x00198050u); return -1; }
    uint32_t v = draw_98050(r, h->sp, cam, player);
    if (failed(r)) return -1;
    *result = (int32_t)v;
    return 0;
}

int em_cam_aim_00198440(EmCamAim *h, uint32_t cam, uint32_t player, int32_t a2)
{
    BEGIN(0x00198440u);
    hold_98440(r, h->sp, cam, player, a2);
    END();
}

int em_cam_aim_00198240(EmCamAim *h, uint32_t player, uint32_t gun, int32_t *result)
{
    BEGIN(0x00198240u);
    if (!result) { fault(r, 1, 0x00198240u); return -1; }
    uint32_t v = wall_98240(r, h->sp, player, gun);
    if (failed(r)) return -1;
    *result = (int32_t)v;
    return 0;
}

int em_cam_aim_001912B0(EmCamAim *h, uint32_t player)
{
    BEGIN(0x001912B0u);
    lift_912B0(r, h->sp, player);
    END();
}

int em_cam_aim_001999C0(EmCamAim *h, uint32_t player, int32_t a1, int32_t *result)
{
    BEGIN(0x001999C0u);
    if (!result) { fault(r, 1, 0x001999C0u); return -1; }
    uint32_t v = sight_999C0(r, h->sp, player, a1);
    if (failed(r)) return -1;
    *result = (int32_t)v;
    return 0;
}

int em_cam_aim_001DB800(EmCamAim *h)
{
    BEGIN(0x001DB800u);
    db800(r);
    END();
}

int em_cam_aim_00198AF0(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x00198AF0u);
    action14_98AF0(r, h->sp, cam, player);
    END();
}

int em_cam_aim_00198930(EmCamAim *h, uint32_t cam, uint32_t player, int32_t *result)
{
    BEGIN(0x00198930u);
    if (!result) { fault(r, 1, 0x00198930u); return -1; }
    uint32_t v = follow_98930(r, h->sp, cam, player);
    if (failed(r)) return -1;
    *result = (int32_t)v;
    return 0;
}

int em_cam_aim_00191530(EmCamAim *h, uint32_t cam, uint32_t player)
{
    BEGIN(0x00191530u);
    raise_91530(r, h->sp, cam, player);
    END();
}
