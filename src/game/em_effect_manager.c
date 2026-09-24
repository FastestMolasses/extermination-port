/* em_effect_manager.c - the effect manager barrel 001F0360 and its workers
 * (see em_effect_manager.h, docs/EFFECT_MANAGER.md).
 *
 * Read from the original instructions: the decomp C where it is
 * byte-matched (001F0360, 001F6210, 001F6EB0, 001F40C0), the split listing
 * for the NEARMISS units 001F0720, 001F6BB0 and 001F1180 (whose readable C
 * has the facing-test polarity inverted; see em_pickup_items_original.c) and
 * for the asm-only units 001F0A60 and 001F4D40. Every original address a
 * branch, store or call comes from is cited beside it. COP1 and VU0 macro
 * arithmetic goes through em_ee_float.h on bit patterns, each VU0 step under
 * its own (op, dest, broadcast) form. */
#include "game/em_effect_manager.h"
#include "game/em_ee_float.h"
#include "game/em_owner_services_original.h"

#include <string.h>

typedef uint32_t u32;

#define F_ZERO  UINT32_C(0x00000000)
#define F_HALF  UINT32_C(0x3F000000)
#define F_ONE   UINT32_C(0x3F800000)
#define F_16    UINT32_C(0x41800000)
#define F_64    UINT32_C(0x42800000)
#define F_80    UINT32_C(0x42A00000)
#define F_128   UINT32_C(0x43000000)
#define F_150   UINT32_C(0x43160000)
#define F_180   UINT32_C(0x43340000)
#define F_PI    UINT32_C(0x40490FDB)
#define F_2M31  UINT32_C(0x30000000) /* 2^-31 */

#define DXYZW 0xFu
#define DXY   0xCu
#define DZ    0x2u
#define DW    0x1u
#define NO_BC EM_VU_NO_BC

static const u32 VF0[4] = {0, 0, 0, F_ONE};

/* ------------------------------------------------------------------ */
/* Faults, byte access.                                                */
/* ------------------------------------------------------------------ */

static int fault_at(EmEffectManager *m, u32 address, int32_t code)
{
    if (m->fault.code == EM_EFFECT_MANAGER_FAULT_NONE) {
        m->fault.address = address;
        m->fault.code = code;
    }
    return -1;
}
#define NEED(ptr, address) \
    do { if (!(ptr)) return fault_at(m, (address), EM_EFFECT_MANAGER_FAULT_NULL_WORKER); } while (0)
#define CALL(address, expr) \
    do { if ((expr) < 0) return fault_at(m, (address), EM_EFFECT_MANAGER_FAULT_WORKER_FAILED); } while (0)
#define FORM(address, expr) \
    do { if ((expr) != EM_EE_FLOAT_OK) return fault_at(m, (address), EM_EFFECT_MANAGER_FAULT_UNMEASURED); } while (0)

static int ready(const EmEffectManager *m) { return m && m->fault.code == EM_EFFECT_MANAGER_FAULT_NONE; }

static u32 rd32(const uint8_t *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
static int16_t rd16(const uint8_t *p) { return (int16_t)(uint16_t)((unsigned)p[0] | (unsigned)p[1] << 8); }
static void wr32(uint8_t *p, u32 v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (u32)v);
    wr32(p + 4, (u32)(v >> 32));
}
static void wrq(uint8_t *p, const u32 v[4])
{
    for (int i = 0; i < 4; ++i) wr32(p + 4 * i, v[i]);
}

/* A static ELF window byte range, or NULL (fault BAD_INDEX). */
static const uint8_t *tdata(EmEffectManager *m, u32 address, u32 size)
{
    static const struct { u32 base, bytes; size_t offset; } win[] = {
        {EM_EFFECT_MANAGER_PAL_BASE, EM_EFFECT_MANAGER_PAL_BYTES, offsetof(EmEffectManagerTables, pal)},
        {EM_EFFECT_MANAGER_AURA_BASE, EM_EFFECT_MANAGER_AURA_BYTES, offsetof(EmEffectManagerTables, aura)},
        {EM_EFFECT_MANAGER_KIND_BASE, EM_EFFECT_MANAGER_KIND_BYTES, offsetof(EmEffectManagerTables, kind)},
        {EM_EFFECT_MANAGER_LIST_BASE, EM_EFFECT_MANAGER_LIST_BYTES, offsetof(EmEffectManagerTables, list)},
        {EM_EFFECT_MANAGER_COLOUR_BASE, EM_EFFECT_MANAGER_COLOUR_BYTES, offsetof(EmEffectManagerTables, colour)},
    };
    if (!m->tables) {
        fault_at(m, address, EM_EFFECT_MANAGER_FAULT_NULL_WORKER);
        return NULL;
    }
    for (size_t i = 0; i < sizeof win / sizeof win[0]; ++i)
        if (address >= win[i].base && size <= win[i].bytes && address - win[i].base <= win[i].bytes - size)
            return (const uint8_t *)m->tables + win[i].offset + (address - win[i].base);
    fault_at(m, address, EM_EFFECT_MANAGER_FAULT_BAD_INDEX);
    return NULL;
}
static int t32(EmEffectManager *m, u32 address, u32 *v)
{
    const uint8_t *p = tdata(m, address, 4);
    if (!p) return -1;
    *v = rd32(p);
    return 0;
}
static int t16(EmEffectManager *m, u32 address, int16_t *v)
{
    const uint8_t *p = tdata(m, address, 2);
    if (!p) return -1;
    *v = rd16(p);
    return 0;
}
static int tq(EmEffectManager *m, u32 address, u32 v[4])
{
    const uint8_t *p = tdata(m, address, 16);
    if (!p) return -1;
    for (int i = 0; i < 4; ++i) v[i] = rd32(p + 4 * i);
    return 0;
}

int em_effect_manager_load_tables(const uint8_t *elf, size_t size, EmEffectManagerTables *out)
{
    /* SCUS_971.12: one PROGBITS section, file 0x300 = vram 0x00100000. */
#define ELF_AT(a) (elf + ((size_t)(a) - 0x00100000u + 0x300u))
    if (!elf || !out || size != 1532624u || memcmp(elf, "\x7F" "ELF", 4)) return -1;
    memcpy(out->pal, ELF_AT(EM_EFFECT_MANAGER_PAL_BASE), sizeof out->pal);
    memcpy(out->aura, ELF_AT(EM_EFFECT_MANAGER_AURA_BASE), sizeof out->aura);
    memcpy(out->kind, ELF_AT(EM_EFFECT_MANAGER_KIND_BASE), sizeof out->kind);
    memcpy(out->list, ELF_AT(EM_EFFECT_MANAGER_LIST_BASE), sizeof out->list);
    memcpy(out->colour, ELF_AT(EM_EFFECT_MANAGER_COLOUR_BASE), sizeof out->colour);
    return 0;
#undef ELF_AT
}

/* ------------------------------------------------------------------ */
/* VU0 leaves on em_ee_float.h.                                        */
/* ------------------------------------------------------------------ */

/* The four-step transform every routine here uses (001026A0's body, and the
 * inline copies in 001F0A60): ACC = r0 * v.x (VMULAx xyzw), ACC += r1 * v.y,
 * ACC += r2 * v.z (VMADDAy/z xyzw), out = ACC + r3 * w (VMADDw xyzw), where
 * w is the w lane of `wsrc` (the vector itself for 001026A0, vf0 = 1.0 for
 * the 001F0A60 copies). */
static int xform(u32 out[4], const u32 r[16], const u32 v[4], const u32 wsrc[4])
{
    u32 acc[4] = {0, 0, 0, 0}, res[4] = {0, 0, 0, 0};
    int st = em_vu_vec_bits(EM_VU_MULABC, DXYZW, 0, r + 0, v, 0, NULL, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, DXYZW, 1, r + 4, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDABC, DXYZW, 2, r + 8, v, 0, acc, acc);
    if (st == EM_EE_FLOAT_OK) st = em_vu_vec_bits(EM_VU_MADDBC, DXYZW, 3, r + 12, wsrc, 0, acc, res);
    if (st == EM_EE_FLOAT_OK) memcpy(out, res, sizeof res);
    return st;
}

/* 001026A0(out, m, v) through the verified em_effect_original translation. */
static void sdk_001026A0(u32 out[4], const u32 mtx[16], const u32 v[4])
{
    float fo[4], fm[16], fv[4];
    memcpy(fm, mtx, sizeof fm);
    memcpy(fv, v, sizeof fv);
    em_effect_original_001026A0(fo, fm, fv);
    memcpy(out, fo, sizeof fo);
}

/* 001026D0(out, a, b): out row i = 001026A0(a, b row i), rows 0..3. */
static void sdk_001026D0(u32 out[16], const u32 a[16], const u32 b[16])
{
    for (int i = 0; i < 4; ++i) sdk_001026A0(out + 4 * i, a, b + 4 * i);
}

static int32_t sdk_float_to_int(u32 bits) /* 001281C0 */
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return em_effect_original_float_to_int(f);
}

/* The VCLIPw.xyz judgement of x/y/z against |w| (bits 0/1 x > |w| / x < -|w|,
 * 2/3 y, 4/5 z). VCLIP is outside the measured float model: finite, DAZ'd
 * operands only; a non-finite lane returns -1 (UNMEASURED). */
static int clip_judge(const u32 v[4], unsigned *flags)
{
    for (int i = 0; i < 4; ++i)
        if (em_eei_exp(v[i]) == 0xFFu) return -1;
    /* Compare magnitudes as integers: finite DAZ'd binary32 values order like
     * their sign-magnitude keys. */
    u32 w = em_eei_daz(v[3]) & UINT32_C(0x7FFFFFFF);
    unsigned f = 0;
    for (int i = 0; i < 3; ++i) {
        u32 c = em_eei_daz(v[i]);
        u32 mag = c & UINT32_C(0x7FFFFFFF);
        int neg = (c & EM_EE_SIGN) != 0;
        if (!neg && mag > w) f |= 1u << (2 * i);
        if (neg && mag > w) f |= 2u << (2 * i);
    }
    *flags = f;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F0720: ring-lane ageing and the lane draw.                       */
/* ------------------------------------------------------------------ */

static void slot_bytes(uint8_t out[0x60], const EmEffectOriginalDecalSlot *s)
{
    for (int i = 0; i < 16; ++i) wr32(out + 4 * i, s->source[i]);
    for (int i = 0; i < 4; ++i) wr32(out + 0x40 + 4 * i, s->params[i]);
    wr64(out + 0x50, s->tag);
    wr32(out + 0x58, (u32)s->life);
    wr32(out + 0x5C, s->w5C);
}

/* Opens `count` quadwords through 001CB5F0 and clears the first one (every
 * packet here starts with a zero quadword store). */
static int open_packet(EmEffectManager *m, u32 chain, int32_t id, int32_t count, uint8_t **out)
{
    const EmEffectManagerWorkers *w = m->workers;
    uint8_t *p = NULL;
    NEED(w->w_001CB5F0, 0x001CB5F0u);
    CALL(0x001CB5F0u, w->w_001CB5F0(w->ctx, chain, id, count, &p));
    NEED(p, 0x001CB5F0u);
    memset(p, 0, 16);
    *out = p;
    return 0;
}

int em_effect_manager_001F0720(EmEffectManager *m, int32_t n)
{
    if (!ready(m)) return -1;
    /* 0x1F074C: presets 0..6 (jtbl_0026E9E0); any other n returns at once. */
    int32_t count;
    u32 scale;
    u32 pal;
    switch (n) {
    case 0: count = 1; scale = F_64; pal = 0x0; break;
    case 1: count = 2; scale = F_64; pal = 0x4; break;
    case 2: count = 2; scale = F_64; pal = 0x4; break;
    case 3: count = 2; scale = F_64; pal = 0x8; break;
    case 4: count = 2; scale = F_80; pal = 0x8; break;
    case 5: count = 1; scale = F_64; pal = 0xC; break;
    case 6: count = 1; scale = F_64; pal = 0xC; break;
    default: return 0;
    }
    NEED(m->workers, 0x001F0720u);
    NEED(m->decals, 0x0028F700u);
    NEED(m->globals, 0x70003A20u);
    NEED(m->view, 0x00275670u);
    NEED(m->workers->w_001CB5F0, 0x001CB5F0u);
    NEED(m->workers->w_001CB760, 0x001CB760u);
    NEED(m->workers->w_001CB900, 0x001CB900u);
    u32 colour[16];
    for (int i = 0; i < 4; ++i)
        if (tq(m, EM_EFFECT_MANAGER_PAL_BASE + pal * 16u + 16u * (u32)i, colour + 4 * i) < 0) return -1;

    /* The age pass (0x1F080C..0x1F08A4). total = count * 60; ftotal is its
     * cvt.s.w. A live countdown (+0x58 > 0) is decremented and reloaded;
     * below total, t / ftotal goes to 0x70003A20 and scale * it to +0x4C.
     * Anything else is stored as 0. */
    EmEffectOriginalDecalSlot *lane = m->decals->slot[n];
    const int32_t total = count * 60;
    const u32 ftotal = em_ee_cvt_s_w_bits((u32)total);
    for (int i = 0; i < 32; ++i) {
        EmEffectOriginalDecalSlot *s = &lane[i];
        int32_t t = s->life;
        if (t > 0) {
            s->life = t - 1;
            t = s->life;
            if (t < total) {
                u32 f = em_ee_div_bits(em_ee_cvt_s_w_bits((u32)t), ftotal);
                m->globals->spad3A20 = f;
                s->params[3] = em_ee_mul_bits(scale, f);
            }
        } else {
            s->life = 0;
        }
    }

    const u32 chain = EM_EFFECT_MANAGER_CHAIN;
    uint8_t *p;
    /* Packet 1 (0x1F08A8): one quadword, words 0x11000000 / 0x14000000 /
     * 0x11000000 / 0. */
    if (open_packet(m, chain, 0, 1, &p) < 0) return -1;
    wr32(p + 0, 0x11000000u);
    wr32(p + 4, 0x14000000u);
    wr32(p + 8, 0x11000000u);

    /* Packet 2 (0x1F08DC, one pass): 0xC1 quadwords, the tag
     * 0x6C000000 | 0xC0 << 16 | 0x20, then the whole 0xC00-byte lane. */
    if (open_packet(m, chain, 0, 0xC1, &p) < 0) return -1;
    wr32(p + 8, 0x01000101u);
    wr32(p + 0xC, 0x20u | (0xC0u << 16) | 0x6C000000u);
    for (int i = 0; i < 32; ++i) slot_bytes(p + 0x10 + 0x60 * i, &lane[i]);

    /* Packet 3 (0x1F094C): 5 quadwords, tag 0x6C04000E, then the preset's four
     * colour quadwords (copy_qw4 from D_00259CD0 + pal * 16). */
    if (open_packet(m, chain, 0, 5, &p) < 0) return -1;
    wr32(p + 8, 0x01000101u);
    wr32(p + 0xC, 0x0Eu | 0x6C040000u);
    for (int i = 0; i < 4; ++i) wrq(p + 0x10 + 16 * i, colour + 4 * i);

    /* Packet 4 (0x1F0994): 0xA quadwords, tag 0x6C090000, then 001CD370(2)
     * (context +0x22C0), the 0x70003AC0 matrix and the context +0xA0
     * quadword (00102948). */
    if (open_packet(m, chain, 0, 0xA, &p) < 0) return -1;
    wr32(p + 8, 0x01000101u);
    wr32(p + 0xC, 0x6C090000u);
    for (int i = 0; i < 4; ++i) wrq(p + 0x10 + 16 * i, m->view->clip2 + 4 * i);
    for (int i = 0; i < 4; ++i) wrq(p + 0x50 + 16 * i, m->view->camera + 4 * i);
    wrq(p + 0x90, m->view->fog);

    const EmEffectManagerWorkers *w = m->workers;
    CALL(0x001CB760u, w->w_001CB760(w->ctx, chain, 0, EM_EFFECT_MANAGER_MICROCODE)); /* 0x1F0A10 */
    CALL(0x001CB900u, w->w_001CB900(w->ctx, chain, 0, 1));                          /* 0x1F0A24 */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F0A60: the camera-facing glint.                                  */
/* ------------------------------------------------------------------ */

/* The colour pass of 0x1F0D34 / 0x1F0E14 on one colour word. f is the
 * 0x7000360C word >> 4 clamped to 0..255 (read afresh for each colour).
 * Mode 1 scales alpha; modes 2, 3 and 4 scale r, g, b and store 0xFF0 at
 * 0x7000360C; other modes leave the colour. */
static u32 modulate(u32 c, int32_t mode, u32 *spad360C)
{
    int32_t f = (int32_t)*spad360C >> 4;
    if (!(f < 0x100)) f = 0xFF;
    if (f < 0) f = 0;
    const u32 uf = (u32)f;
    if (mode == 1) {
        u32 a = ((c >> 24) & 0xFFu) * uf;
        return (c & 0x00FFFFFFu) | ((a >> 8) << 24);
    }
    if (mode == 4 || mode == 3 || mode == 2) {
        u32 r = ((c & 0xFFu) * uf) >> 8;
        u32 g = (((c >> 8) & 0xFFu) * uf) >> 8;
        u32 b = (((c >> 16) & 0xFFu) * uf) >> 8;
        *spad360C = 0xFF0u;
        return (c & 0xFF000000u) | b << 16 | g << 8 | r;
    }
    return c;
}

int em_effect_manager_001F0A60(EmEffectManager *m, int32_t a0, int32_t mode, const u32 pos[4],
                               u32 colour_a3, u32 colour_t0, u32 f12, u32 f13, u32 f14)
{
    if (!ready(m)) return -1;
    NEED(pos, 0x001F0A60u);
    NEED(m->workers, 0x001F0A60u);
    NEED(m->globals, 0x70003600u);
    NEED(m->view, 0x00275670u);
    const EmEffectManagerWorkers *w = m->workers;
    NEED(w->w_0021B9A0, 0x0021B9A0u);
    NEED(w->w_001CB5F0, 0x001CB5F0u);
    NEED(w->w_001CB900, 0x001CB900u);
    EmEffectManagerGlobals *g = m->globals;
    EmEffectManagerView *view = m->view;

    /* 0x1F0AA8..0x1F0AF4: v = 001CD370(0) * (pos.xyz, 1); VCLIPw.xyz; any of
     * the six flags returns with nothing done. */
    u32 v[4];
    FORM(0x001F0AC4u, xform(v, view->clip0, pos, VF0));
    unsigned flags;
    if (clip_judge(v, &flags) < 0) return fault_at(m, 0x001F0AD4u, EM_EFFECT_MANAGER_FAULT_UNMEASURED);
    if (flags & 0x3Fu) return 0;

    /* 0x1F0B0C / 0x1F0B24: 0021B9A0(2, 1.0, 150.0) and (3, 1.0, 150.0). */
    CALL(0x0021B9A0u, w->w_0021B9A0(w->ctx, 2, F_ONE, F_150));
    CALL(0x0021B9A0u, w->w_0021B9A0(w->ctx, 3, F_ONE, F_150));

    /* 0x1F0B2C..0x1F0BB0: the fog quadword (read now, after the calls) and
     * the 0x70003AC0 transform. The unscaled vector is kept (stack +0x90);
     * xy *= 1/w; w -= f14; z *= 1/(w - f14); w = clamp(fog.z + fog.w * w,
     * 0, fog.x); all four lanes to 12.4 fixed point at 0x70003600. */
    u32 fog[4];
    memcpy(fog, view->fog, sizeof fog);
    FORM(0x001F0B60u, xform(v, view->camera, pos, VF0));
    const u32 kept_w = v[3];
    u32 q;
    FORM(0x001F0B70u, em_vu_div_bits(VF0[3], v[3], 3, 3, &q));
    FORM(0x001F0B7Cu, em_vu_vec_bits(EM_VU_MULQ, DXY, NO_BC, v, NULL, q, NULL, v));
    const u32 vf3[4] = {f14, 0, 0, 0};
    FORM(0x001F0B8Cu, em_vu_vec_bits(EM_VU_SUBBC, DW, 0, v, vf3, 0, NULL, v));
    FORM(0x001F0B90u, em_vu_div_bits(VF0[3], v[3], 3, 3, &q));
    FORM(0x001F0B98u, em_vu_vec_bits(EM_VU_MULQ, DZ, NO_BC, v, NULL, q, NULL, v));
    u32 acc[4] = {0, 0, 0, 0};
    FORM(0x001F0B9Cu, em_vu_vec_bits(EM_VU_MULABC, DW, 2, VF0, fog, 0, NULL, acc));
    FORM(0x001F0BA0u, em_vu_vec_bits(EM_VU_MADDBC, DW, 3, fog, v, 0, acc, v));
    v[3] = em_vu_min_bits(v[3], fog[0]);  /* 0x1F0BA4: VMINIx.w with fog.x */
    v[3] = em_vu_max_bits(v[3], VF0[0]);  /* 0x1F0BA8: VMAXx.w with vf0.x (+0) */
    for (int i = 0; i < 4; ++i) g->spad3600[i] = em_vu_ftoi4_bits(v[i]);

    /* 0x1F0BB4..0x1F0C54: 0x70003400 = (f13, 0, 0, 1), 0x70003410 = (0, f13,
     * 0, 1); 0x70003440 = identity rotated about z by f12 (001029C0,
     * 00102A60); both corners are rotated by it (001026A0, in place). */
    u32 *spr = g->spad3400;
    spr[0] = f13; spr[1] = 0; spr[2] = 0; spr[3] = F_ONE;
    spr[4] = 0; spr[5] = f13; spr[6] = 0; spr[7] = F_ONE;
    float rot[16];
    FORM(0x001029C0u, em_owner_services_identity_001029C0(rot));
    FORM(0x00102A60u, em_owner_services_rotate_z_00102A60(rot, rot, f12));
    memcpy(spr + 16, rot, sizeof rot);
    sdk_001026A0(spr + 0, spr + 16, spr + 0);
    sdk_001026A0(spr + 4, spr + 16, spr + 4);

    /* 0x1F0C58..0x1F0D30: each corner c: vf1 = (0.5 * c.x, 0.5 * c.y, kept w);
     * the 0x70003A40 rows with row 2 xy cleared (VSUB.xy of row 2 with
     * itself) transform it with w = 1; xy *= 1/w; VFTOI4 xy. */
    int32_t off[2][2];
    for (int k = 0; k < 2; ++k) {
        const u32 *c = spr + 4 * k;
        u32 half_y = em_ee_mul_bits(F_HALF, c[1]);
        u32 half_x = em_ee_mul_bits(F_HALF, c[0]);
        const u32 vf1[4] = {half_x, half_y, kept_w, 0};
        u32 rows[16];
        memcpy(rows, view->screen, sizeof rows);
        FORM(0x001F0CA4u, em_vu_vec_bits(EM_VU_SUB, DXY, NO_BC, rows + 8, rows + 8, 0, NULL, rows + 8));
        u32 s[4];
        FORM(0x001F0CA8u, xform(s, rows, vf1, VF0));
        FORM(0x001F0CB8u, em_vu_div_bits(VF0[3], s[3], 3, 3, &q));
        FORM(0x001F0CC0u, em_vu_vec_bits(EM_VU_MULQ, DXY, NO_BC, s, NULL, q, NULL, s));
        off[k][0] = (int32_t)em_vu_ftoi4_bits(s[0]);
        off[k][1] = (int32_t)em_vu_ftoi4_bits(s[1]);
    }

    /* 0x1F0D34 / 0x1F0E14: with a nonzero mode, the a3 colour and then the t0
     * colour pass through the fog weight (0x7000360C re-read for each). */
    u32 ca = colour_a3, cb = colour_t0;
    if (mode != 0) {
        ca = modulate(ca, mode, &g->spad3600[3]);
        cb = modulate(cb, mode, &g->spad3600[3]);
    }

    /* 0x1F0EF4..0x1F1094: 0xE quadwords on chain D_0028F700 + (a0 << 15) +
     * 0x4D3EC0 with id = the 0x70003608 word: VIF 0x5000000D, the GIF tag
     * 0x6035400000008002 / regs 0x414141, then two passes of three vertices
     * (t0 colour at centre + offset, a3 colour at the centre, t0 colour at
     * centre - offset); the first vertex's fog word gets bit 15. */
    const u32 chain = EM_EFFECT_MANAGER_CHAIN + ((u32)a0 << 15);
    uint8_t *p;
    if (open_packet(m, chain, (int32_t)g->spad3600[2], 0xE, &p) < 0) return -1;
    wr32(p + 0xC, 0x5000000Du);
    wr64(p + 0x10, UINT64_C(0x6035400000008002));
    wr64(p + 0x18, UINT64_C(0x0000000000414141));
    const u32 *sp = g->spad3600;
    for (int k = 0; k < 2; ++k) {
        uint8_t *v3 = p + 0x20 + 0x60 * k;
        for (int i = 0; i < 4; ++i) {
            wr32(v3 + 0x00 + 4 * (u32)i, (cb >> (8 * i)) & 0xFFu);
            wr32(v3 + 0x20 + 4 * (u32)i, (ca >> (8 * i)) & 0xFFu);
            wr32(v3 + 0x40 + 4 * (u32)i, (cb >> (8 * i)) & 0xFFu);
        }
        wr32(v3 + 0x10, sp[0] + (u32)off[k][0]);
        wr32(v3 + 0x14, sp[1] + (u32)off[k][1]);
        wr32(v3 + 0x18, sp[2]);
        wr32(v3 + 0x1C, sp[3] | 0x8000u);
        wr32(v3 + 0x30, sp[0]);
        wr32(v3 + 0x34, sp[1]);
        wr32(v3 + 0x38, sp[2]);
        wr32(v3 + 0x3C, sp[3]);
        wr32(v3 + 0x50, sp[0] - (u32)off[k][0]);
        wr32(v3 + 0x54, sp[1] - (u32)off[k][1]);
        wr32(v3 + 0x58, sp[2]);
        wr32(v3 + 0x5C, sp[3]);
    }

    /* 0x1F10C8: 001CB900(chain, 0x70003608 word, mode); 0x1F10D8:
     * 0021B9A0(1, 0, 0). */
    CALL(0x001CB900u, w->w_001CB900(w->ctx, chain, (int32_t)g->spad3600[2], mode));
    CALL(0x0021B9A0u, w->w_0021B9A0(w->ctx, 1, F_ZERO, F_ZERO));
    return 0;
}

/* ------------------------------------------------------------------ */
/* The draw block of 001F1180 (0x1F136C..0x1F1470).                    */
/* ------------------------------------------------------------------ */

int em_effect_manager_aura_draw(EmEffectManager *m, const u32 owner_d0[16], u32 record, u32 angle,
                                u32 timer)
{
    if (!ready(m)) return -1;
    NEED(owner_d0, 0x001F1180u);
    NEED(m->workers, 0x001F1180u);
    const EmEffectManagerWorkers *w = m->workers;
    NEED(w->w_0011E2A8, 0x0011E2A8u);
    u32 rec[11];
    for (u32 i = 0; i < 11; ++i)
        if (t32(m, record + 4u * i, &rec[i]) < 0) return -1;

    /* 0x1F136C: s = 0011E2A8(pi * timer); size = rec+0x24 * s. */
    u32 s;
    CALL(0x0011E2A8u, w->w_0011E2A8(w->ctx, em_ee_mul_bits(F_PI, timer), &s));
    const u32 size = em_ee_mul_bits(rec[9], s);
    /* 0x1F138C..0x1F13F8: the two colour words, alpha float_to_int(128 * s)
     * and float_to_int(16 * s). */
    u32 c16 = rec[3] | rec[4] << 8 | rec[5] << 16;
    u32 c128 = rec[0] | rec[1] << 8 | rec[2] << 16;
    c128 |= (u32)sdk_float_to_int(em_ee_mul_bits(F_128, s)) << 24;
    c16 |= (u32)sdk_float_to_int(em_ee_mul_bits(F_16, s)) << 24;
    /* 0x1F13F0..0x1F1424: the record's +0x18 offset (w = 1) through the
     * owner's +0xD0 matrix (001026A0, in place). */
    u32 at[4] = {rec[6], rec[7], rec[8], F_ONE};
    sdk_001026A0(at, owner_d0, at);
    /* 0x1F1428..0x1F146C: f12 = (pi * angle) / 180, f13 = size, f14 =
     * rec+0x28; 001F0A60(0, 1, at, c128, c16, ...). */
    const u32 f12 = em_ee_div_bits(em_ee_mul_bits(F_PI, angle), F_180);
    return em_effect_manager_001F0A60(m, 0, 1, at, c128, c16, f12, size, rec[10]);
}

/* ------------------------------------------------------------------ */
/* 001F4D40: the rand-pulsed 001CD520 sprite.                          */
/* ------------------------------------------------------------------ */

int em_effect_manager_001F4D40(EmEffectManager *m, u32 position, const u32 colour[4], u32 f12, u32 f13)
{
    if (!ready(m)) return -1;
    NEED(colour, 0x001F4D40u);
    NEED(m->workers, 0x001F4D40u);
    const EmEffectManagerWorkers *w = m->workers;
    NEED(w->w_00122BB8, 0x00122BB8u);
    NEED(w->w_001CD520, 0x001CD520u);
    /* 0x1F4D5C..0x1F4DFC: k = (rand >> 23) & 0xFF (arithmetic shift);
     * p = (3 * a + (a * k >> 8)) >> 2 with a = colour +0xC (32-bit products,
     * logical shifts); rgb = (b * p >> 7) << 16 | (g * p >> 7) << 8 |
     * (r * p >> 7) with r, g, b = colour +0, +4, +8. */
    int32_t r;
    CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &r));
    const u32 a = colour[3];
    const u32 k = (u32)(r >> 23) & 0xFFu;
    const u32 t = (a * 3u + ((a * k) >> 8)) >> 2;
    const u32 rgb = ((colour[2] * t) >> 7) << 16 | ((colour[1] * t) >> 7) << 8 | ((colour[0] * t) >> 7);
    /* 0x1F4DF8: 001CD520(0, 2, position, 0x20045B0599421EF0, rgb, f12, f12, f13). */
    CALL(0x001CD520u, w->w_001CD520(w->ctx, 0, 2, position, UINT64_C(0x20045B0599421EF0), rgb, f12,
                                    f12, f13));
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F40C0: the entity sweep.                                         */
/* ------------------------------------------------------------------ */

int em_effect_manager_001F40C0(EmEffectManager *m)
{
    if (!ready(m)) return -1;
    NEED(m->workers, 0x001F40C0u);
    NEED(m->globals, 0x00275C44u);
    NEED(m->entities, EM_EFFECT_MANAGER_ENTITY_BASE);
    const EmEffectManagerWorkers *w = m->workers;
    for (int i = 0; i < EM_EFFECT_MANAGER_ENTITIES; ++i) {
        EmEffectManagerEntity *e = &m->entities[i];
        const u32 at = EM_EFFECT_MANAGER_ENTITY_BASE + EM_EFFECT_MANAGER_ENTITY_STRIDE * (u32)i;
        if (e->live != 0) continue;
        /* 001F3620(entity, +0x82); then, still live, 001F3E30(0x700036A0,
         * entity + 0x40, rec +0x50, rec +0x54 + (i % 2) * 4, rec +0x5C) with
         * rec = D_0025A350 + (+0x82, re-read) * 0x60. */
        NEED(w->w_001F3620, 0x001F3620u);
        CALL(0x001F3620u, w->w_001F3620(w->ctx, at, e->kind, e));
        if (e->live != 0) continue;
        const u32 rec = EM_EFFECT_MANAGER_KIND_BASE + (u32)((int32_t)e->kind * 0x60);
        u32 x, y, z;
        if (t32(m, rec + 0x50u, &x) < 0 || t32(m, rec + 0x54u + 4u * (u32)(i % 2), &y) < 0 ||
            t32(m, rec + 0x5Cu, &z) < 0)
            return -1;
        NEED(w->w_001F3E30, 0x001F3E30u);
        CALL(0x001F3E30u, w->w_001F3E30(w->ctx, EM_EFFECT_MANAGER_SCRATCH_36A0, at + 0x40u, (int32_t)x,
                                        (int32_t)y, (int32_t)z));
    }
    m->globals->d275C44 -= 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F6BB0 / 001F6EB0: the area selectors.                            */
/* ------------------------------------------------------------------ */

/* 001F6AC0(p): the word at p +0x24 is not -1. */
static int slot_ready(EmEffectManager *m, u32 record, int *ready_out)
{
    const EmEffectManagerWorkers *w = m->workers;
    u32 v;
    NEED(w->w_word, record + 0x24u);
    CALL(record + 0x24u, w->w_word(w->ctx, record + 0x24u, &v));
    *ready_out = v != 0xFFFFFFFFu;
    return 0;
}

int em_effect_manager_001F6BB0(EmEffectManager *m)
{
    if (!ready(m)) return -1;
    NEED(m->workers, 0x001F6BB0u);
    NEED(m->globals, 0x00810700u);
    const EmEffectManagerWorkers *w = m->workers;
    const EmEffectManagerGlobals *g = m->globals;
    const int32_t key = ((int32_t)g->d810700 << 8) + g->d810701;
    /* 0x1F6BD8..0x1F6C14: keys 0x1100, 0xE00, 0x200, 0x100, 2 and 1 return. */
    if (key == 0x1100 || key == 0xE00 || key == 0x200 || key == 0x100 || key == 2 || key == 1) return 0;
    int r;
    if (key == 0x1301) {
        const u32 sub = g->d810702;
        if (sub == 2 || sub - 4u < 2u || sub == 8) {
            /* 0x1F6CAC: either slot ready calls 001F6850. */
            if (slot_ready(m, EM_EFFECT_MANAGER_TABLE_D25D270, &r) < 0) return -1;
            if (!r && slot_ready(m, EM_EFFECT_MANAGER_TABLE_D25D2C0, &r) < 0) return -1;
            if (r) {
                NEED(w->w_001F6850, 0x001F6850u);
                CALL(0x001F6850u, w->w_001F6850(w->ctx));
            }
            return 0;
        }
        /* 0x1F6CDC: slot 0 not ready and D_00810778, D_0081077B both not
         * 0xFF: 001F6640(D_0025D270). 0x1F6D1C: slot 1 not ready and
         * D_0081079E == 0xFF: 001F6640(D_0025D2C0). */
        if (slot_ready(m, EM_EFFECT_MANAGER_TABLE_D25D270, &r) < 0) return -1;
        if (!r && g->d810778 != 0xFF && g->d81077B != 0xFF) {
            NEED(w->w_001F6640, 0x001F6640u);
            CALL(0x001F6640u, w->w_001F6640(w->ctx, EM_EFFECT_MANAGER_TABLE_D25D270));
        }
        if (slot_ready(m, EM_EFFECT_MANAGER_TABLE_D25D2C0, &r) < 0) return -1;
        if (!r && g->d81079E == 0xFF) {
            NEED(w->w_001F6640, 0x001F6640u);
            CALL(0x001F6640u, w->w_001F6640(w->ctx, EM_EFFECT_MANAGER_TABLE_D25D2C0));
        }
        return 0;
    }
    if (key != 0) return 0;
    /* 0x1F6C38: key 0 with D_0081075D == 0xFF: p = 001F6760(); 001F66F0(p)
     * when p is ready. */
    if (g->d81075D != 0xFF) return 0;
    u32 p;
    NEED(w->w_001F6760, 0x001F6760u);
    CALL(0x001F6760u, w->w_001F6760(w->ctx, &p));
    if (slot_ready(m, p, &r) < 0) return -1;
    if (r) {
        NEED(w->w_001F66F0, 0x001F66F0u);
        CALL(0x001F66F0u, w->w_001F66F0(w->ctx, p));
    }
    return 0;
}

int em_effect_manager_001F6EB0(EmEffectManager *m)
{
    if (!ready(m)) return -1;
    NEED(m->workers, 0x001F6EB0u);
    NEED(m->globals, 0x00810700u);
    const EmEffectManagerWorkers *w = m->workers;
    const EmEffectManagerGlobals *g = m->globals;
    const u32 hi = (u32)g->d810700 << 8;
    const u32 code = hi + g->d810701;
    int32_t track;
    if (code == 0x700) track = g->d25D524;       /* D_0025D524 */
    else if (code == 0x1200) track = g->d25D6E4; /* D_0025D6E4 */
    else return 0;
    if (track == -1) {
        if (g->d810702 != 1) {
            NEED(w->w_001F6E40, 0x001F6E40u);
            CALL(0x001F6E40u, w->w_001F6E40(w->ctx, g->d810702, hi));
        }
    } else if (g->d810702 == 1) {
        NEED(w->w_001F6E80, 0x001F6E80u);
        CALL(0x001F6E80u, w->w_001F6E80(w->ctx, g->d810702, hi));
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F6210: the area model sprites.                                   */
/* ------------------------------------------------------------------ */

static uint8_t *list_at(EmEffectManager *m, u32 address, u32 size)
{
    uint8_t *p = m->workers->w_list_at(m->workers->ctx, address, size);
    if (!p) fault_at(m, address, EM_EFFECT_MANAGER_FAULT_BAD_INDEX);
    return p;
}

int em_effect_manager_001F6210(EmEffectManager *m)
{
    if (!ready(m)) return -1;
    NEED(m->workers, 0x001F6210u);
    NEED(m->globals, 0x00810700u);
    const EmEffectManagerWorkers *w = m->workers;
    EmEffectManagerGlobals *g = m->globals;
    u32 list = 0;
    NEED(w->w_001F5CA0, 0x001F5CA0u);
    CALL(0x001F5CA0u, w->w_001F5CA0(w->ctx, &list));
    /* 0x1F6254: key base (((area << 8) + room) << 8); a 0 list returns. */
    const u32 base = (((u32)g->d810700 << 8) + g->d810701) << 8;
    if (list == 0) return 0;
    NEED(m->view, 0x70003AC0u);
    NEED(w->w_001D8C20, 0x001D8C20u);
    NEED(w->w_001C6120, 0x001C6120u);
    NEED(w->w_00122BB8, 0x00122BB8u);
    NEED(w->w_001D3D90, 0x001D3D90u);
    NEED(w->w_001CAAC0, 0x001CAAC0u);
    NEED(w->w_list_at, 0x00275670u);
    CALL(0x001D8C20u, w->w_001D8C20(w->ctx, 1));

    u32 rec = list;
    for (;;) {
        int16_t head, id;
        if (t16(m, rec, &head) < 0) return -1;
        if (head < 0) break;
        if (t16(m, rec + 4u, &id) < 0) return -1;
        u32 r[9];
        for (u32 i = 0; i < 6; ++i)
            if (t32(m, rec + 0xCu + 4u * i, &r[i]) < 0) return -1;
        /* 0x1F626C: owner = the context +0x1C cursor; handle =
         * 001C6120(D_0028A59C, +4). */
        const u32 owner = g->list_cursor;
        u32 handle;
        CALL(0x001C6120u, w->w_001C6120(w->ctx, g->d28A59C, id, &handle));
        /* 0x1F6284..0x1F62E8: sp80 = (+0x18, +0x1C, +0x20, 0) angles; sp70 =
         * (+0xC, +0x10, +0x14, 1.0) position; spA0 = identity, 00102C58,
         * 00102918. */
        u32 sp80[4] = {r[3], r[4], r[5], 0};
        u32 sp70[4] = {r[0], r[1], r[2], F_ONE};
        float mf[16], fa[3], fp[3];
        memcpy(fa, sp80, sizeof fa);
        memcpy(fp, sp70, sizeof fp);
        FORM(0x001029C0u, em_owner_services_identity_001029C0(mf));
        FORM(0x00102C58u, em_owner_services_euler_00102C58(mf, mf, fa));
        FORM(0x00102918u, em_owner_services_translate_00102918(mf, mf, fp));
        u32 spA0[16];
        memcpy(spA0, mf, sizeof spA0);

        /* 0x1F62EC..0x1F6360: colour row 0 of D_0026EB20, replaced by the row
         * of the first of the 17 D_0025CA40 keys equal to +4 + base. */
        const u32 key = (u32)(int32_t)id + base;
        u32 sp90[4];
        if (tq(m, EM_EFFECT_MANAGER_COLOUR_BASE, sp90) < 0) return -1;
        for (u32 i = 0; i < 0x11u; ++i) {
            u32 k;
            if (t32(m, EM_EFFECT_MANAGER_LIST_BASE + 8u * i, &k) < 0) return -1;
            if (key == k) {
                int16_t row;
                if (t16(m, EM_EFFECT_MANAGER_LIST_BASE + 8u * i + 4u, &row) < 0) return -1;
                if (tq(m, EM_EFFECT_MANAGER_COLOUR_BASE + (u32)((int32_t)row * 16), sp90) < 0) return -1;
                break;
            }
        }
        /* 0x1F6364..0x1F63E8: t = -a + (a - -a) * (2^-31 * rand) with a =
         * colour.w; rgb += rgb * t; colour.w = 0. */
        const u32 az = sp90[3];
        const u32 negaz = em_ee_neg_bits(az);
        int32_t rnd;
        CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &rnd));
        u32 f2 = em_ee_mul_bits(F_2M31, em_ee_cvt_s_w_bits((u32)rnd));
        u32 f0 = em_ee_mul_bits(em_ee_sub_bits(az, negaz), f2);
        const u32 t = em_ee_add_bits(negaz, f0);
        for (int i = 0; i < 3; ++i) sp90[i] = em_ee_add_bits(sp90[i], em_ee_mul_bits(sp90[i], t));
        sp90[3] = 0;

        /* 0x1F63EC..0x1F6414: block type 5 header (+3 = 0x10, +4 = 0, +0 =
         * 5); the cursor moves 0x60. */
        uint8_t *q = list_at(m, g->list_cursor, 0x60);
        if (!q) return -1;
        q[3] = 0x10;
        wr32(q + 4, 0);
        wr16(q + 0, 5);
        g->list_cursor += 0x60u;
        /* 0x1F6418..0x1F64F4: 0x70003400..0x7000346F cleared; 0x70003470 =
         * colour + D_0026EB60 (001028B8, VADD xyzw). */
        for (int i = 0; i < 28; ++i) g->spad3400[i] = 0;
        u32 add[4];
        if (tq(m, EM_EFFECT_MANAGER_COLOUR_BASE + 0x40u, add) < 0) return -1;
        FORM(0x001028B8u, em_vu_vec_bits(EM_VU_ADD, DXYZW, NO_BC, sp90, add, 0, NULL, g->spad3400 + 28));
        /* 0x1F64F8..0x1F6558: tag 0x11000000 / 0x01000101 / 0 / 0x6C0403F5,
         * then 0x70003440..0x7000347F. */
        wr32(q + 0x10, 0x11000000u);
        wr32(q + 0x14, 0x01000101u);
        wr32(q + 0x18, 0);
        wr32(q + 0x1C, 0x6C0403F5u);
        for (int i = 0; i < 4; ++i) wrq(q + 0x20 + 16 * i, g->spad3400 + 16 + 4 * i);

        /* 0x1F655C..0x1F65B0: block type 9 (+3 = 0x10, +4 = 0, +0 = 9), cursor
         * + 0xA0; tag 0 / 0x01000101 / 0 / 0x6C080000; 001026D0(0x70003AC0,
         * spA0) and 001026D0(0x70003400, spA0). */
        q = list_at(m, g->list_cursor, 0xA0);
        if (!q) return -1;
        q[3] = 0x10;
        wr32(q + 4, 0);
        wr16(q + 0, 9);
        g->list_cursor += 0xA0u;
        memset(q + 0x10, 0, 16);
        wr32(q + 0x14, 0x01000101u);
        wr32(q + 0x18, 0);
        wr32(q + 0x1C, 0x6C080000u);
        u32 prod[16];
        sdk_001026D0(prod, m->view->camera, spA0);
        for (int i = 0; i < 4; ++i) wrq(q + 0x20 + 16 * i, prod + 4 * i);
        sdk_001026D0(prod, g->spad3400, spA0);
        for (int i = 0; i < 4; ++i) wrq(q + 0x60 + 16 * i, prod + 4 * i);

        CALL(0x001D3D90u, w->w_001D3D90(w->ctx, handle)); /* 0x1F65B4 */

        /* 0x1F65BC..0x1F65F0: block type 0 (+3 = 0x60, +4 = 0, +0 = 0),
         * cursor + 0x10; 001CAAC0(sp70, owner, context). */
        q = list_at(m, g->list_cursor, 0x10);
        if (!q) return -1;
        q[3] = 0x60;
        wr32(q + 4, 0);
        wr16(q + 0, 0);
        g->list_cursor += 0x10u;
        CALL(0x001CAAC0u, w->w_001CAAC0(w->ctx, sp70, owner, g->d275670));
        rec += 0x28u;
    }
    CALL(0x001D8C20u, w->w_001D8C20(w->ctx, 0)); /* 0x1F6604 */
    return 0;
}

/* ------------------------------------------------------------------ */
/* 001F0360: the barrel.                                               */
/* ------------------------------------------------------------------ */

int em_effect_manager_001F0360(EmEffectManager *m)
{
    if (!ready(m)) return -1;
    NEED(m->workers, 0x001F0360u);
    /* Fail-stop before any write: everything the barrel reaches on every
     * call (the 001F5CA0 selector, 001F5C20, the 001F40C0 state and the
     * 001F0720 lanes and chain) must be bound. Workers of the conditional
     * paths are checked where their path is decided. */
    const EmEffectManagerWorkers *w = m->workers;
    NEED(m->globals, 0x00810700u);
    NEED(m->entities, EM_EFFECT_MANAGER_ENTITY_BASE);
    NEED(m->decals, 0x0028F700u);
    NEED(m->view, 0x00275670u);
    NEED(w->w_001F5CA0, 0x001F5CA0u);
    NEED(w->w_001F5C20, 0x001F5C20u);
    NEED(w->w_001CB5F0, 0x001CB5F0u);
    NEED(w->w_001CB760, 0x001CB760u);
    NEED(w->w_001CB900, 0x001CB900u);
    if (em_effect_manager_001F6210(m) < 0) return -1;
    NEED(m->workers->w_001F5C20, 0x001F5C20u);
    CALL(0x001F5C20u, m->workers->w_001F5C20(m->workers->ctx));
    if (em_effect_manager_001F6BB0(m) < 0) return -1;
    if (em_effect_manager_001F6EB0(m) < 0) return -1;
    if (em_effect_manager_001F40C0(m) < 0) return -1;
    static const int32_t lanes[6] = {0, 1, 3, 4, 5, 6};
    for (int i = 0; i < 6; ++i)
        if (em_effect_manager_001F0720(m, lanes[i]) < 0) return -1;
    return 0;
}
