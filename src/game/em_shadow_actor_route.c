/* em_shadow_actor_route.c - the blob shadow of the +0x214 != 0 route (see
 * em_shadow_actor_route.h and docs/SHADOW_ACTOR_ROUTE.md).
 *
 * Read from the original instructions (build/asm of the decomp), not from the
 * readable decompilation: 0015BF90 and 001F8D30 are NEARMISS C, 001026D0 /
 * 00102900 / 00102948 hand-written asm. Where the readable 001F8D30 C
 * differs from its instructions:
 *   - the depth lane of each projected corner uses vf23 as 001F8D30 leaves
 *     it, i.e. the decal matrix's row 3 (loaded at 001F8EAC), not a global
 *     fog register: depth = max(min(row3.z + row3.w * clip.w, row3.x), 0);
 *   - x, y and z are multiplied by Q = 1 / clip.w (vdiv then vmulq), not
 *     divided by w.
 * Every address in a comment is the original instruction translated there.
 * tools/test_shadow_actor_route_reference.py executes the original
 * instructions and compares every scratch word and every worker call. */
#include "game/em_shadow_actor_route.h"

#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"
#include "game/em_sdk_math_original.h"
#include "game/em_stream_lanes_original.h"

#include <string.h>

/* Float constants as the instructions build them (lui / ori). */
#define F_ZERO  UINT32_C(0x00000000)
#define F_ONE   UINT32_C(0x3F800000)
#define F_100   UINT32_C(0x42C80000)   /* 0015C0A0 */
#define F_4_2   UINT32_C(0x40866666)   /* 0015C050 / 0015C0F4: the half extent */
#define F_30    UINT32_C(0x41F00000)   /* 001F911C: the fade range */
#define F_0_1   UINT32_C(0x3DCCCCCD)   /* 001F8E20 / 001F8E24: the lowest fade */
/* 001F90B0..001F90D0: the TEX0 word 001F8D30 hands 001CE300. */
#define DECAL_TEX0 UINT64_C(0x2004290511322469)

/* ---- faults ---------------------------------------------------------------- */

static int fail(EmShadowActorRoute *r, uint32_t address, int32_t code)
{
    if (r->fault.code == EM_SHADOW_ACTOR_ROUTE_FAULT_NONE) {
        r->fault.address = address;
        r->fault.code = code;
    }
    return -1;
}

#define WORKER(r, address, expr) \
    do { if ((expr) < 0) return fail((r), (address), EM_SHADOW_ACTOR_ROUTE_FAULT_WORKER); } while (0)
#define VU(r, address, expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fail((r), (address), EM_SHADOW_ACTOR_ROUTE_FAULT_FLOAT); } while (0)

/* ---- ELF data ----------------------------------------------------------------- */

static uint32_t elf_word(const uint8_t *elf, uint32_t address)
{
    const uint8_t *b = elf + (address - 0x00100000u + 0x300u);   /* file 0x300 = vram 0x00100000 */
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int em_shadow_actor_route_load_tables(const uint8_t *elf, size_t size,
                                      EmShadowActorRouteTables *out)
{
    if (!elf || !out || size != EM_SHADOW_ACTOR_ROUTE_ELF_SIZE || memcmp(elf, "\x7F" "ELF", 4))
        return -1;
    for (unsigned i = 0; i < 4; ++i) {
        out->colour[i] = elf_word(elf, 0x0025DAE0u + 4u * i);
        out->facing[i] = elf_word(elf, 0x0025DAF0u + 4u * i);
    }
    return 0;
}

/* ---- the SDK VU0 leaves translated here ----------------------------------------- */

static const uint32_t kVF0[4] = { F_ZERO, F_ZERO, F_ZERO, F_ONE };

/* One row through the matrix rows m[0..3] with the last term's weight `w3`:
 * ACC = m0 * v.x; ACC += m1 * v.y; ACC += m2 * v.z; out = ACC + m3 * w3.w.
 * (the four-step VU0 multiply-accumulate chain, all four lanes) */
static int vu_row(uint32_t out[4], const uint32_t m[16], const uint32_t v[4], const uint32_t w3[4])
{
    uint32_t acc[4] = { 0, 0, 0, 0 };
    int st = em_vu_vec_bits(EM_VU_MULABC, 15, 0, m + 0, v, 0, NULL, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 1, m + 4, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, 15, 2, m + 8, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDBC, 15, 3, m + 12, w3, 0, acc, out);
    return st;
}

/* 001026D0(dst, a, b): the rows of `a` are loaded first (001026D0..001026DC),
 * then each row of `b` goes through them with its own w (the vmaddw's
 * broadcast is the row's w, 001026F4) and is stored to the same row of dst.
 * dst may alias a or b: each row of b is read before its row of dst is
 * written, and a is held in registers. */
static int vu_product_001026D0(uint32_t dst[16], const uint32_t a[16], const uint32_t b[16])
{
    uint32_t m[16];
    memcpy(m, a, sizeof m);
    for (unsigned row = 0; row < 4; ++row) {
        uint32_t v[4], out[4] = { 0, 0, 0, 0 };
        memcpy(v, b + 4 * row, sizeof v);
        int st = vu_row(out, m, v, v);
        if (st != EM_EE_FLOAT_OK) return st;
        memcpy(dst + 4 * row, out, sizeof out);
    }
    return EM_EE_FLOAT_OK;
}

/* 00102900(dst, v, s): dst = v * s in all four lanes (s is the x lane of the
 * register the float is moved into). */
static int vu_scale_00102900(uint32_t dst[4], const uint32_t v[4], uint32_t s)
{
    const uint32_t t[4] = { s, 0, 0, 0 };
    uint32_t out[4] = { 0, 0, 0, 0 };
    int st = em_vu_vec_bits(EM_VU_MULBC, 15, 0, v, t, 0, NULL, out);
    if (st == EM_EE_FLOAT_OK) memcpy(dst, out, sizeof out);
    return st;
}

/* ---- float-array bridges for the reused owner-services leaves ------------------- */

static void to_floats(float out[16], const uint32_t in[16]) { memcpy(out, in, 64); }
static void to_words(uint32_t out[16], const float in[16]) { memcpy(out, in, 64); }

/* ---- 001F8D30 ------------------------------------------------------------------ */

static int reach_001F8D30(const EmShadowActorRoute *r)
{
    const EmShadowActorRouteWorkers *w = r->workers;
    return w && w->atan2 && w->look_at && w->submit && r->scratch.s3600 && r->scratch.s3AC0;
}

/* The body, after the entry checks. */
static int decal_001F8D30(EmShadowActorRoute *r, const uint32_t owner[4], const uint32_t point[4],
                          const uint32_t normal[4], const uint32_t facing[4],
                          const uint32_t colour[4], uint32_t half_w, uint32_t half_h,
                          uint32_t range)
{
    const EmShadowActorRouteWorkers *w = r->workers;
    uint32_t basis[16], surf[16], yaw = 0;
    float fm[16], fv[3];

    /* 1. The decal matrix (sp+0x70). */
    VU(r, 0x001029C0u, em_owner_services_identity_001029C0(fm));           /* 001F8D78 */
    to_words(basis, fm);
    WORKER(r, 0x0011E620u, w->atan2(w->context, facing[2], facing[0], &yaw)); /* 001F8D84: y = +8, x = +0 */
    to_floats(fm, basis);
    VU(r, 0x00102A60u, em_owner_services_rotate_z_00102A60(fm, fm, yaw));  /* 001F8D94 */
    to_words(basis, fm);
    WORKER(r, 0x001CD390u, w->look_at(w->context, surf, normal));         /* 001F8DA0 (sp+0xB0) */
    VU(r, 0x001026D0u, vu_product_001026D0(basis, surf, basis));          /* 001F8DB0 */
    to_floats(fm, basis);
    memcpy(fv, point, sizeof fv);                                          /* only x, y, z are added */
    VU(r, 0x00102918u, em_owner_services_translate_00102918(fm, fm, fv));  /* 001F8DC0 */
    to_words(basis, fm);

    /* 2. The fade (001F8DD0..001F8E54). */
    uint32_t fade;
    if (em_ee_c_eq_bits(F_ZERO, range)) {                                  /* 001F8DD0 */
        fade = F_ONE;                                                      /* 001F8E4C */
    } else {
        float d = em_ee_float(em_ee_sub_bits(owner[1], point[1]));         /* 001F8DEC */
        uint32_t a = em_ee_bits(em_sdk_math_original_0011DF78(d));        /* 001F8DE8 fabsf */
        fade = em_ee_sub_bits(F_ONE, em_ee_div_bits(a, range));            /* 001F8DF8, 001F8E08 */
        if (!em_ee_c_le_bits(fade, F_ONE))                                 /* 001F8E0C */
            fade = F_ONE;                                                  /* 001F8E1C */
        if (em_ee_c_lt_bits(fade, F_0_1))                                  /* 001F8E30 */
            fade = F_0_1;                                                  /* 001F8E44 */
    }

    /* 3. The colour (00102900 at 001F8E58) and its four bytes. */
    uint32_t tinted[4];
    VU(r, 0x00102900u, vu_scale_00102900(tinted, colour, fade));
    uint32_t rgba = em_stream_lanes_00128250(tinted[0]);                   /* 001F8E60 */
    rgba |= em_stream_lanes_00128250(tinted[1]) << 8;                      /* 001F8E6C */
    rgba |= em_stream_lanes_00128250(tinted[2]) << 16;                     /* 001F8E7C */
    rgba |= em_stream_lanes_00128250(tinted[3]) << 24;                     /* 001F8E8C */

    /* 4. The four corners (sp+0xF0.., local z = 0, w word 1.0), each through
     * the matrix rows vf20..vf23 (001F8EA0..001F8EAC) with vf0.w as the
     * last weight; negations at 001F8EB0 / 001F8EB4. */
    const uint32_t nw = em_ee_neg_bits(half_w), nh = em_ee_neg_bits(half_h); /* 001F8EAC / 001F8EB0 */
    const uint32_t local[4][2] = { { nw, nh }, { half_w, nh }, { nw, half_h }, { half_w, half_h } };
    uint32_t corner[16];
    for (unsigned i = 0; i < 4; ++i) {
        const uint32_t v[4] = { local[i][0], local[i][1], F_ZERO, F_ONE };
        uint32_t out[4] = { 0, 0, 0, 0 };
        VU(r, 0x001F8EE4u + 0x2Cu * i, vu_row(out, basis, v, kVF0));       /* stored at 001F8EE4, F10, F3C, F68 */
        memcpy(corner + 4 * i, out, sizeof out);
    }

    /* 5. The first three corners through the camera (vf28..vf31 =
     * 0x70003AC0) into 0x70003600 / 0x70003610 / 0x70003620. vf23 still
     * holds the decal matrix's row 3. */
    const uint32_t *row3 = basis + 12;
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t address = 0x001F8FC8u + 0x48u * i;                  /* stored at 001F8FC8, 9010, 9058 */
        uint32_t clip[4] = { 0, 0, 0, 0 }, acc[4] = { 0, 0, 0, 0 }, q = 0;
        VU(r, address, vu_row(clip, r->scratch.s3AC0, corner + 4 * i, kVF0));
        VU(r, address, em_vu_div_bits(F_ONE, clip[3], 3, 3, &q));           /* Q = 1.0 / clip.w */
        VU(r, address, em_vu_vec_bits(EM_VU_MULQ, 14, EM_VU_NO_BC, clip, NULL, q, NULL, clip));
        VU(r, address, em_vu_vec_bits(EM_VU_MULABC, 1, 2, kVF0, row3, 0, NULL, acc));
        VU(r, address, em_vu_vec_bits(EM_VU_MADDBC, 1, 3, row3, clip, 0, acc, clip));
        clip[3] = em_vu_min_bits(clip[3], row3[0]);                        /* depth = min(depth, row3.x) */
        clip[3] = em_vu_max_bits(clip[3], kVF0[0]);                        /* depth = max(depth, 0) */
        for (unsigned k = 0; k < 4; ++k)
            r->scratch.s3600[4 * i + k] = em_vu_ftoi4_bits(clip[k]);       /* to 12.4 fixed, stored */
    }

    /* 6. The screen winding of P0, P1, P2 (001F905C..001F90A4): 32-bit
     * wrapping differences and products (mult / mult1 keep the low word). */
    const uint32_t *s = r->scratch.s3600;
    uint32_t dx01 = s[4] - s[0], dy12 = s[9] - s[5], dy01 = s[5] - s[1], dx12 = s[8] - s[4];
    uint32_t cross = dx01 * dy12 - dy01 * dx12;
    if (cross & UINT32_C(0x80000000))                                      /* 001F90A8 bltz */
        return 0;

    /* 7. 001CE300(1, corners, tex0, rgba) (001F90CC). */
    WORKER(r, 0x001CE300u, w->submit(w->context, 1, corner, DECAL_TEX0, rgba));
    return 0;
}

int em_shadow_actor_route_001F8D30(EmShadowActorRoute *r, const uint32_t owner[4],
                                   const uint32_t point[4], const uint32_t normal[4],
                                   const uint32_t facing[4], const uint32_t colour[4],
                                   uint32_t f12, uint32_t f13, uint32_t f14)
{
    if (!r || r->fault.code != EM_SHADOW_ACTOR_ROUTE_FAULT_NONE) return -1;
    if (!owner || !point || !normal || !facing || !colour || !reach_001F8D30(r))
        return fail(r, 0x001F8D30u, EM_SHADOW_ACTOR_ROUTE_FAULT_UNBOUND);
    return decal_001F8D30(r, owner, point, normal, facing, colour, f12, f13, f14);
}

/* ---- 001F9100 ------------------------------------------------------------------ */

static int call_001F9100(EmShadowActorRoute *r, const uint32_t owner[4], const uint32_t point[4],
                         const uint32_t normal[4], uint32_t f12)
{
    /* The facing is a stack copy of D_0025DAF0 (001F9114 / 001F9124), the
     * colour D_0025DAE0 itself (t0); f13 = f12, f14 = 30.0. */
    uint32_t facing[4];
    memcpy(facing, r->tables->facing, sizeof facing);
    return decal_001F8D30(r, owner, point, normal, facing, r->tables->colour, f12, f12, F_30);
}

int em_shadow_actor_route_001F9100(EmShadowActorRoute *r, const uint32_t owner[4],
                                   const uint32_t point[4], const uint32_t normal[4], uint32_t f12)
{
    if (!r || r->fault.code != EM_SHADOW_ACTOR_ROUTE_FAULT_NONE) return -1;
    if (!owner || !point || !normal || !r->tables || !reach_001F8D30(r))
        return fail(r, 0x001F9100u, EM_SHADOW_ACTOR_ROUTE_FAULT_UNBOUND);
    return call_001F9100(r, owner, point, normal, f12);
}

/* ---- 0015BF90 ------------------------------------------------------------------ */

int em_shadow_actor_route_0015BF90(EmShadowActorRoute *r, const EmPlayerLiveActor *p)
{
    if (!r || r->fault.code != EM_SHADOW_ACTOR_ROUTE_FAULT_NONE) return -1;
    const EmShadowActorRouteScratch *sc = &r->scratch;
    const EmShadowActorRouteWorkers *w = r->workers;
    if (!p || !r->tables || !sc->s38A0 || !sc->s38B0 || !sc->s3A20 || !sc->s3B8D ||
        !reach_001F8D30(r) || !w->node_c4 || !w->segment)
        return fail(r, 0x0015BF90u, EM_SHADOW_ACTOR_ROUTE_FAULT_UNBOUND);

    if (em_live_u8(p, 0x1F0) == 0x19)                                      /* 0015BFA8 */
        return 0;

    uint32_t *a = sc->s38A0, *b = sc->s38B0, owner[4];
    for (unsigned i = 0; i < 4; ++i) owner[i] = em_live_u32(p, 0xB0 + 4 * i);
    memcpy(a, owner, sizeof owner);                                        /* 0015BFB8 00102948 */

    /* The lower of the two node heights (0015BFC0..0015BFE8): c.lt.s a, b;
     * a when a < b, else b (also when either is a NaN). */
    uint32_t y154 = 0, y158 = 0;
    WORKER(r, 0x0015BFC8u, w->node_c4(w->context, 0x154, em_live_u32(p, 0x154), &y154));
    WORKER(r, 0x0015BFCCu, w->node_c4(w->context, 0x158, em_live_u32(p, 0x158), &y158));
    const uint32_t low = em_ee_c_lt_bits(y154, y158) ? y154 : y158;
    *sc->s3A20 = low;                                                      /* 0015BFF0 */
    a[1] = low;                                                            /* 0015BFF8 */

    if (*sc->s3B8D != 0 && em_live_u8(p, 0x1F0) == 0x41) {                 /* 0015C004 / 0015C014 */
        /* One unit below the start point, normal +y, no query. */
        b[0] = F_ZERO;                                                     /* 0015C020 */
        b[1] = F_ONE;                                                      /* 0015C03C */
        b[2] = F_ZERO;                                                     /* 0015C044 */
        b[3] = F_ONE;                                                      /* 0015C04C */
        a[1] = em_ee_sub_bits(a[1], F_ONE);                                /* 0015C060, stored 0015C078 */
        return call_001F9100(r, owner, a, b, F_4_2);                       /* 0015C074 */
    }

    memcpy(b, a, 16);                                                      /* 0015C090 00102948 */
    b[1] = em_ee_sub_bits(b[1], F_100);                                    /* 0015C0B4, stored 0015C0C4 */
    int32_t hit = 0;
    uint32_t point[4] = { 0, 0, 0, 0 }, normal[3] = { 0, 0, 0 };
    uint32_t from[4], to[4];
    memcpy(from, a, sizeof from);
    memcpy(to, b, sizeof to);
    WORKER(r, 0x0019A570u, w->segment(w->context, from, to, 6, 0, &hit, point, normal)); /* 0015C0C8 */
    if (hit == 0)                                                          /* 0015C0D0 */
        return 0;
    memcpy(a, point, 16);                                                  /* 0015C0E4 00102948(38A0, 31B0) */
    b[0] = normal[0];                                                      /* 0015C118 */
    b[1] = normal[1];                                                      /* 0015C12C */
    b[2] = normal[2];                                                      /* 0015C138 */
    b[3] = F_ONE;                                                          /* 0015C144 */
    return call_001F9100(r, owner, a, b, F_4_2);                           /* 0015C140 */
}
