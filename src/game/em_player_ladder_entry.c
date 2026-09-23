/* em_player_ladder_entry.c - the Use surface actions and the ladder entry
 * state (see em_player_ladder_entry.h, docs/PLAYER_LADDER_ENTRY.md).
 *
 * Read from the original instructions (build/asm of the decomp), not from the
 * readable decompilation alone: 0015D4C0, 00177030, 00199DB0 and 00199FA0
 * are NEARMISS C, 0017FC80 and 00182A70 unmarked readable C; 00165B60,
 * 00176DC0, 00176F90, 00180300 and 001B61C0 are byte-matched. Where the
 * NEARMISS C differs from its instructions:
 *   - 00177030 mode 4 stores the dot product at 0x70003A24 (00177408)
 *     whether or not the gate passes; the C keeps it in a local;
 *   - 0015D4C0 cases 0x37/0x38: `found` is only cleared when 0019AD00 hits
 *     (0015D61C). On a miss it is the caller's $s0, which 00160220 (the only
 *     caller) holds as the actor pointer (00160248): nonzero, so a miss takes
 *     the found path; the C reads an uninitialized local;
 *   - 0015D4C0 case 0x3D reads the stack word 00199FA0 fills (0015DE68)
 *     whether or not 00199FA0 wrote it.
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_ladder_entry_reference.py executes the original
 * instructions and compares every actor byte, scratch word, return value and
 * worker call. */
#include "game/em_player_ladder_entry.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Float constants as the instructions build them (lui/ori). */
#define F_ZERO    UINT32_C(0x00000000)
#define F_ONE     UINT32_C(0x3F800000)
#define F_M1      UINT32_C(0xBF800000)
#define F_HALF    UINT32_C(0x3F000000)
#define F_1_5     UINT32_C(0x3FC00000)
#define F_2       UINT32_C(0x40000000)
#define F_3       UINT32_C(0x40400000)
#define F_3_8     UINT32_C(0x40733333)
#define F_4_01    UINT32_C(0x408051EC)
#define F_5_5     UINT32_C(0x40B00000)
#define F_8       UINT32_C(0x41000000)
#define F_10      UINT32_C(0x41200000)
#define F_M10     UINT32_C(0xC1200000)
#define F_16      UINT32_C(0x41800000)
#define F_18      UINT32_C(0x41900000)
#define F_20      UINT32_C(0x41A00000)
#define F_20_5    UINT32_C(0x41A40000)
#define F_24      UINT32_C(0x41C00000)
#define F_25      UINT32_C(0x41C80000)
#define F_30      UINT32_C(0x41F00000)
#define F_40      UINT32_C(0x42200000)
#define F_52      UINT32_C(0x42500000)
#define F_63      UINT32_C(0x427C0000)
#define F_M0_2    UINT32_C(0xBE4CCCCD)
#define F_PI      UINT32_C(0x40490FDB)
#define F_HALF_PI UINT32_C(0x3FC90FDB)
#define F_QPI     UINT32_C(0x3F490FDB)   /* 0.7853982 */
#define F_M_QPI   UINT32_C(0xBF490FDB)
/* 00165B60 case 0's box and case 11's pose. */
#define F_1010    UINT32_C(0x447C8000)
#define F_1030    UINT32_C(0x4480C000)
#define F_170     UINT32_C(0x432A0000)
#define F_180     UINT32_C(0x43340000)
#define F_830     UINT32_C(0x444F8000)
#define F_850     UINT32_C(0x44548000)
#define F_1021_9  UINT32_C(0x447F799A)
#define F_187_2   UINT32_C(0x433B3333)
#define F_836_3   UINT32_C(0x44511333)
/* 00165B60 case 2's offset (0, -10.4, 6.3, 0). */
#define F_M10_4   UINT32_C(0xC1266666)
#define F_6_3     UINT32_C(0x40C9999A)

/* D_002488B0 (24.0). */
#define D_002488B0 F_24
/* D_0024895C: 00176DC0's five probe yaws (pi/2, -pi/2, 3pi/4, -3pi/4, pi). */
static const uint32_t kWallYaw[5] = {
    UINT32_C(0x3FC90FDB), UINT32_C(0xBFC90FDB), UINT32_C(0x4016CBE4), UINT32_C(0xC016CBE4),
    UINT32_C(0x40490FDB)
};

static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void put32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static void words(const EmPlayerLiveActor *a, unsigned at, uint32_t *out, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) out[i] = w32(a, at + 4 * i);
}
static void put_words(EmPlayerLiveActor *a, unsigned at, const uint32_t *in, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) put32(a, at + 4 * i, in[i]);
}
static int le(uint32_t x, uint32_t y) { return em_ee_c_le_bits(x, y); }
static int lt(uint32_t x, uint32_t y) { return em_ee_c_lt_bits(x, y); }

int em_player_ladder_workers_bound(const EmPlayerLadderWorkers *w)
{
    return w && w->scratch && w->world && w->probe_0019BA80 && w->move_0019AD00 &&
           w->segment_0019A570 && w->sweep_0019AFE0 && w->column_0019BC40 && w->trs &&
           w->apply && w->vadd && w->identity && w->euler && w->translate && w->rotate_y &&
           w->dot && w->normalize && w->atan2_0011E620 && w->wrap_001B1470 &&
           w->fabs_0011DF78 && w->cos_0011DE90 && w->sqrt_0011E748 && w->request && w->sound &&
           w->sound_base_00179B90 && w->clip_001885D0 && w->clip_001885F0 &&
           w->wall_001762E0 && w->node;
}

/* ---- the probe record ---------------------------------------------------- */

/* A read of the record 0x700031D0 names at `site`: with no record the
 * original reads low memory, which the native world does not have. */
static int record_ok(EmPlayerLadderScratch *s, uint32_t site)
{
    if (s->record == EM_PLAYER_LADDER_RECORD_CELL || s->record == EM_PLAYER_LADDER_RECORD_OTHER)
        return 0;
    if (!s->fault) s->fault = site;
    return -1;
}
static uint32_t rec32(const EmPlayerLadderScratch *s, unsigned at)
{
    uint32_t v; memcpy(&v, s->record_bytes + at, 4); return v;
}
static int16_t rec16(const EmPlayerLadderScratch *s, unsigned at)
{
    uint16_t v; memcpy(&v, s->record_bytes + at, 2); return (int16_t)v;
}

/* ---- 00102948 and 001031E0 ---------------------------------------------- */

/* 00102948(dst, src): the quadword copy (lq, sq); a third argument some
 * callers pass is not read. */
static void copy4(uint32_t dst[4], const uint32_t src[4])
{
    uint32_t t[4];
    memcpy(t, src, sizeof t);
    memcpy(dst, t, sizeof t);
}
/* 001031E0(dst, src): three words (lwc1/swc1, no conversion). */
static void copy3(uint32_t dst[3], const uint32_t src[3])
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}

/* ---- 00199DB0 / 00199FA0 --------------------------------------------------- */

/* The cell path shared by both (00199DD0.. / 00199FC0..): the owner at
 * 0x700031D4, 0x700031D8 == 2, uid = owner +0x0E >> 8 (not 0xFF), the
 * directory's offset word for uid (not 0). *hull is the six words there, or
 * NULL when the original returns 0. */
static int cell_hull(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *s, uint32_t site,
                     const uint8_t **hull)
{
    *hull = NULL;
    if (s->entity == 0) return 0;                                      /* 00199DD8 */
    if (s->s31D8 != 2) return 0;                                       /* 00199DEC */
    unsigned uid = (unsigned)s->entity_0E >> 8;                        /* 00199DFC */
    if (uid == 0xFF) return 0;                                         /* 00199E00 */
    if (!world->directory || (uint64_t)uid * 4 + 8 > world->directory_size) {
        if (!s->fault) s->fault = site;
        return -1;
    }
    uint32_t offset;
    memcpy(&offset, world->directory + uid * 4 + 4, 4);                /* 00199E18 */
    if (offset == 0) return 0;                                         /* 00199E1C */
    if ((uint64_t)offset + 0x18 > world->directory_size) {
        if (!s->fault) s->fault = site;
        return -1;
    }
    *hull = world->directory + offset;
    return 0;
}

/* The grid path: vertex `index` lane `lane` of *0x700031FC. */
static int vertex(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *s, int index,
                  unsigned lane, uint32_t site, uint32_t *out)
{
    if (!world->verts || index < 0 || (uint32_t)index >= world->vert_count) {
        if (!s->fault) s->fault = site;
        return -1;
    }
    *out = world->verts[(uint32_t)index * 3 + lane];
    return 0;
}

int em_player_ladder_00199DB0(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *s,
                              uint32_t out[3], int *result)
{
    if (!world || !s || !out || !result) return -1;
    *result = 0;
    if (s->record == EM_PLAYER_LADDER_RECORD_NONE) return 0;           /* 00199DB8 */
    uint32_t value[3];
    if (s->record == EM_PLAYER_LADDER_RECORD_CELL) {                   /* 00199DC8 */
        const uint8_t *hull;
        FAULT(cell_hull(world, s, 0x00199E18, &hull));
        if (!hull) return 0;
        for (unsigned k = 0; k < 3; ++k) {                             /* 00199E34.. */
            uint32_t lo, hi;
            memcpy(&lo, hull + 4 * k, 4);
            memcpy(&hi, hull + 0xC + 4 * k, 4);
            value[k] = em_ee_div_bits(em_ee_add_bits(lo, hi), F_2);
        }
    } else if (s->record == EM_PLAYER_LADDER_RECORD_OTHER) {
        /* 00199E94..: each lane the mean of the lane of the two vertices the
         * record's halfword pairs (+0/+2, +4/+6, +8/+A) name. */
        for (unsigned k = 0; k < 3; ++k) {
            uint32_t va, vb;
            FAULT(vertex(world, s, rec16(s, 4 * k), k, 0x00199EBC + 0x54 * k, &va));
            FAULT(vertex(world, s, rec16(s, 4 * k + 2), k, 0x00199ED8 + 0x50 * k, &vb));
            value[k] = em_ee_div_bits(em_ee_add_bits(va, vb), F_2);
        }
    } else {
        if (!s->fault) s->fault = 0x00199DB4;
        return -1;
    }
    copy3(out, value);
    *result = 1;
    return 0;
}

int em_player_ladder_00199FA0(const EmPlayerLadderWorld *world, EmPlayerLadderScratch *s,
                              uint32_t a[3], uint32_t b[3], int *result)
{
    if (!world || !s || !a || !b || !result) return -1;
    *result = 0;
    if (s->record == EM_PLAYER_LADDER_RECORD_NONE) return 0;           /* 00199FA8 */
    uint32_t lo[3], hi[3];
    if (s->record == EM_PLAYER_LADDER_RECORD_CELL) {                   /* 00199FB8 */
        const uint8_t *hull;
        FAULT(cell_hull(world, s, 0x0019A008, &hull));
        if (!hull) return 0;
        memcpy(lo, hull, 12);                                          /* 0019A024.. */
        memcpy(hi, hull + 0xC, 12);
    } else if (s->record == EM_PLAYER_LADDER_RECORD_OTHER) {
        for (unsigned k = 0; k < 3; ++k) {                             /* 0019A068.. */
            FAULT(vertex(world, s, rec16(s, 4 * k), k, 0x0019A088, &lo[k]));
            FAULT(vertex(world, s, rec16(s, 4 * k + 2), k, 0x0019A0B4, &hi[k]));
        }
    } else {
        if (!s->fault) s->fault = 0x00199FA4;
        return -1;
    }
    copy3(a, lo);
    copy3(b, hi);
    *result = 1;
    return 0;
}

/* ---- 00176F90 ------------------------------------------------------------ */

static int refresh(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    uint32_t b0[4], box[4];
    words(a, 0xB0, b0, 4);
    copy4(s->s38A0, b0);                                               /* 00176FA8 */
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_M0_2);                 /* 00176FC8 */
    words(a, 0x280, box, 4);
    int hit = 0;
    FAULT(w->probe_0019BA80(w->context, a, s->s38A0, box, 7, &hit));   /* 00176FE0 */
    if (hit != 0) {
        FAULT(record_ok(s, 0x00176FF8));
        put8(a, 0x23B, s->record_bytes[0x1A]);                          /* 00176FFC */
    } else if (b8(a, 0x23B) != 0x35) {                                 /* 0017700C */
        put8(a, 0x23B, 0);                                             /* 00177014 */
    }
    *result = b8(a, 0x23B);                                            /* 00177018 */
    return 0;
}

/* ---- 00177030 ------------------------------------------------------------ */

static int center(const EmPlayerLadderWorkers *w, uint32_t out[3])
{
    int ignored = 0;
    return em_player_ladder_00199DB0(w->world, w->scratch, out, &ignored);
}

static int facing(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int mode, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    *result = 0;
    FAULT(record_ok(s, 0x00177054));
    uint32_t angle = 0;
    FAULT(w->atan2_0011E620(c, em_ee_neg_bits(rec32(s, 0x3C)), rec32(s, 0x34), &angle));
    s->s3A20[0] = angle;                                               /* 00177068 */
    FAULT(w->wrap_001B1470(c, em_ee_add_bits(F_HALF_PI, s->s3A20[0]), &angle));
    s->s3A20[0] = angle;                                               /* 0017708C */
    FAULT(w->wrap_001B1470(c, em_ee_sub_bits(s->s3A20[0], w32(a, 0xC4)), &angle));
    s->s3A20[1] = angle;                                               /* 001770B0 */

    if (mode == 4) {                                                   /* 00177358 */
        FAULT(record_ok(s, 0x00177370));
        s->s38B0[0] = rec32(s, 0x34);
        s->s38B0[1] = F_ZERO;
        s->s38B0[2] = rec32(s, 0x3C);
        s->s38B0[3] = F_ONE;
        FAULT(w->normalize(c, s->s38B0, s->s38B0));                    /* 00177398 */
        s->s38C0[0] = F_ZERO;
        s->s38C0[1] = F_ZERO;
        s->s38C0[2] = F_ONE;
        s->s38C0[3] = F_ZERO;
        uint32_t m[16];
        words(a, 0xD0, m, 16);
        FAULT(w->apply(c, s->s38D0, m, s->s38C0));                     /* 001773D4 */
        uint32_t dot = 0;
        FAULT(w->dot(c, s->s38B0, s->s38D0, &dot));                    /* 001773E8 */
        s->s3A20[1] = dot;                                             /* 00177408 */
        if (lt(dot, F_HALF)) return 0;                                 /* 00177404 */
        put32(a, 0xC4, s->s3A20[0]);                                   /* 00177420 */
        FAULT(center(w, s->s38A0));                                    /* 0017741C */
        put32(a, 0xB0, s->s38A0[0]);
        put32(a, 0xB8, s->s38A0[2]);
        *result = 1;
        return 0;
    }
    if (mode == 3) {                                                   /* 0017732C */
        FAULT(center(w, s->s38B0));
        put32(a, 0xB0, s->s38B0[0]);
        put32(a, 0xB8, s->s38B0[2]);
        *result = 1;
        return 0;
    }
    if (mode == 2) {                                                   /* 001771C0 */
        uint32_t t = 0, side;
        FAULT(w->fabs_0011DF78(c, s->s3A20[1], &t));
        if (le(t, F_HALF_PI)) {                                        /* 001771DC */
            put32(a, 0x218, s->s3A20[0]);                              /* 00177200 */
            side = F_ONE;
        } else {
            uint32_t yaw = 0;
            FAULT(w->wrap_001B1470(c, em_ee_add_bits(F_PI, s->s3A20[0]), &yaw));
            side = F_M1;
            put32(a, 0x218, yaw);                                      /* 00177228 */
        }
        FAULT(center(w, s->s38B0));                                    /* 00177230 */
        s->s3A20[0] = em_ee_sub_bits(s->s38B0[0], w32(a, 0xB0));       /* 00177254 */
        s->s3A20[2] = em_ee_sub_bits(s->s38B0[2], w32(a, 0xB8));       /* 0017726C */
        uint32_t heading = 0;
        FAULT(w->atan2_0011E620(c, em_ee_neg_bits(s->s3A20[2]), s->s3A20[0], &heading));
        s->s3A20[1] = heading;                                         /* 0017727C */
        uint32_t acc = em_ee_mula_bits(s->s3A20[0], s->s3A20[0]);      /* 00177290 */
        uint32_t length = 0;
        FAULT(w->sqrt_0011E748(c, em_ee_madd_bits(acc, s->s3A20[2], s->s3A20[2]), &length));
        s->s3A20[3] = length;                                          /* 001772A0 */
        uint32_t turn = 0, cosine = 0;
        FAULT(w->wrap_001B1470(c, em_ee_sub_bits(em_ee_add_bits(F_HALF_PI, s->s3A20[1]),
                                                 w32(a, 0x218)), &turn));
        FAULT(w->cos_0011DE90(c, turn, &cosine));                      /* 001772C8 */
        uint32_t step = em_ee_mul_bits(s->s3A20[3], cosine);           /* 001772E4 */
        FAULT(record_ok(s, 0x001772E8));
        put32(a, 0xB0, em_ee_sub_bits(s->s38B0[0],
                                      em_ee_mul_bits(side, em_ee_mul_bits(rec32(s, 0x34), step))));
        put32(a, 0xB8, em_ee_sub_bits(s->s38B0[2],
                                      em_ee_mul_bits(side, em_ee_mul_bits(rec32(s, 0x3C), step))));
        *result = 1;
        return 0;
    }
    if (mode == 1) {                                                   /* 00177140 */
        if (lt(s->s3A20[1], F_M_QPI)) return 0;                        /* 00177158 */
        if (!le(s->s3A20[1], F_QPI)) return 0;                         /* 00177178 */
        put32(a, 0xC4, s->s3A20[0]);                                   /* 0017719C */
        FAULT(center(w, s->s38B0));
        put32(a, 0xB0, s->s38B0[0]);
        put32(a, 0xB8, s->s38B0[2]);
        *result = 1;
        return 0;
    }
    if (mode == 0) {                                                   /* 001770E8 */
        if (lt(s->s3A20[1], F_M_QPI)) return 0;                        /* 00177100 */
        if (!le(s->s3A20[1], F_QPI)) return 0;                         /* 00177120 */
        put32(a, 0xC4, s->s3A20[0]);                                   /* 0017713C */
        *result = 1;
    }
    return 0;                                                          /* 001770E0 */
}

/* ---- 00180300 ------------------------------------------------------------ */

static int classify(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, const uint32_t v[4],
                    int check, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    uint32_t m[16];
    s->s3600[0] = F_ZERO;                                              /* 00180318 */
    s->s3600[1] = F_ZERO;
    s->s3600[2] = F_10;
    s->s3600[3] = F_ZERO;                                              /* 00180358 */
    words(a, 0xD0, m, 16);
    FAULT(w->apply(c, s->s3610, m, s->s3600));                         /* 00180354 */
    FAULT(w->vadd(c, s->s3610, s->s3610, v));                          /* 0018036C */
    int hit = 0;
    FAULT(w->sweep_0019AFE0(c, a, v, s->s3610, 6, &hit));              /* 00180384 */
    if (hit == 0) {                                                    /* 0018038C */
        *result = 2;
        return 0;
    }
    FAULT(record_ok(s, 0x0018039C));
    put8(a, 0x23B, s->record_bytes[0x1A]);                              /* 001803A4 */
    uint8_t attr = b8(a, 0x23B);
    if (check == 0) *result = attr == 0x32 ? 0 : 1;                    /* 001803B0 */
    else if (check == 1) *result = attr == 0x3B ? 0 : 1;               /* 001803D8 */
    else if (check == 2) *result = attr == 0x33 ? 0 : 1;               /* 001803FC */
    else *result = 1;                                                  /* 001803C4 */
    return 0;
}

/* ---- 0015D4C0 ------------------------------------------------------------ */

static int trs(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    uint32_t out[16], position[4], rotation[4], scale[4];
    words(a, 0xB0, position, 4);
    words(a, 0xC0, rotation, 4);
    words(a, 0x60, scale, 4);
    FAULT(w->trs(w->context, out, position, rotation, scale));
    put_words(a, 0xD0, out, 16);
    return 0;
}

/* 001026A0(p+B0, p+D0, 0x700038A0) after 0x700038A0 = (0, 0, -1, 1): one
 * unit back along the body (0015D790.. and 0015D8E0..). */
static int step_back(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    EmPlayerLadderScratch *s = w->scratch;
    s->s38A0[0] = F_ZERO;
    s->s38A0[1] = F_ZERO;
    s->s38A0[2] = F_M1;
    s->s38A0[3] = F_ONE;
    uint32_t m[16], out[4];
    words(a, 0xD0, m, 16);
    FAULT(w->apply(w->context, out, m, s->s38A0));
    put_words(a, 0xB0, out, 4);
    return 0;
}

/* Cases 0x37 / 0x38 (0015D550..0015D7D4). */
static int action_ledge(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    int ok = 0;
    FAULT(facing(w, a, 1, &ok));                                       /* 0015D554 */
    if (!ok) return 0;
    put8(a, 5, 0x18);                                                  /* 0015D568 */
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x2C);
    if (b8(a, 0x23B) == 0x37) {                                        /* 0015D580 */
        put8(a, 0xD, 0);                                               /* 0015D58C */
        *result = 1;
        return 0;
    }
    FAULT(trs(w, a));                                                  /* 0015D59C */
    s->s38A0[0] = F_ZERO;
    s->s38A0[1] = F_4_01;
    s->s38A0[2] = F_10;
    s->s38A0[3] = F_ONE;
    uint32_t m[16];
    words(a, 0xD0, m, 16);
    FAULT(w->apply(c, s->s38B0, m, s->s38A0));                         /* 0015D5E4 */
    int hit = 0;
    FAULT(w->move_0019AD00(c, a, s->s38B0, 7, &hit));                  /* 0015D5F8 */
    /* $s0: cleared below on a hit; on a miss it is 00160220's actor
     * pointer (00160248), which is nonzero. */
    int found = 1;
    if (hit != 0) {                                                    /* 0015D600 */
        FAULT(record_ok(s, 0x0015D628));
        found = 0;                                                     /* 0015D61C */
        s->s38C0[0] = em_ee_sub_bits(s->s31B0[0], rec32(s, 0x24));     /* 0015D644 */
        s->s38C0[2] = em_ee_sub_bits(s->s31B0[2], rec32(s, 0x2C));     /* 0015D654 */
        s->s38C0[1] = s->s38B0[1];                                     /* 0015D65C */
        s->s38C0[3] = F_ONE;                                           /* 0015D668 */
        FAULT(w->column_0019BC40(c, s->s38C0));                        /* 0015D664 */
        int32_t n = s->s31E0;                                          /* 0015D670 */
        for (int32_t i = 0; i < n; ++i) {                              /* 0015D720 */
            if (i >= EM_PLAYER_LADDER_COLUMN_MAX) {
                if (!s->fault) s->fault = 0x0015D6A0;
                return -1;
            }
            if (!(s->s3170[i] & 1)) continue;                          /* 0015D6A8 */
            uint32_t feet = w32(a, 0xB4);
            if (le(s->s30F0[i], em_ee_add_bits(F_4_01, feet))) continue;   /* 0015D6BC */
            uint32_t d = s->s30F0[i];                                  /* 0015D6E4 */
            if (!lt(d, em_ee_add_bits(feet, D_002488B0))) found = 1;   /* 0015D6EC */
            put32(a, 0x254, d);                                        /* 0015D704 / 0015D70C */
            break;
        }
    }
    if (found) {                                                       /* 0015D730 */
        put8(a, 0xD, 3);                                               /* 0015D73C */
        FAULT(record_ok(s, 0x0015D750));
        put32(a, 0xB0, em_ee_add_bits(s->s31B0[0], em_ee_mul_bits(F_1_5, rec32(s, 0x24))));
        put32(a, 0xB8, em_ee_add_bits(s->s31B0[2], em_ee_mul_bits(F_1_5, rec32(s, 0x2C))));
    } else {
        put8(a, 0xD, 1);                                               /* 0015D790 */
        FAULT(step_back(w, a));                                        /* 0015D7C8 */
    }
    *result = 1;
    return 0;
}

/* Case 0x32: the ladder columns (0015D7DC..0015D870). */
static int action_ladder(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    int ok = 0;
    FAULT(facing(w, a, 4, &ok));                                       /* 0015D7DC */
    if (!ok) return 0;
    FAULT(trs(w, a));                                                  /* 0015D7F8 */
    put8(a, 0xD, 0);                                                   /* 0015D804 */
    put8(a, 5, 0xB);                                                   /* 0015D808 */
    put8(a, 6, 0);                                                     /* 0015D81C */
    uint32_t b0[3];
    words(a, 0xB0, b0, 3);
    copy3(s->s38A0, b0);                                               /* 0015D818 */
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_10);                   /* 0015D84C */
    int r = 0;
    FAULT(classify(w, a, s->s38A0, 0, &r));                            /* 0015D848 */
    put8(a, 0x1F0, r == 0 ? 0x15 : 0x16);                              /* 0015D860 / 0015D868 */
    *result = 1;
    return 0;
}

/* Case 0x3B (0015D878..0015D8A4). */
static int action_3b(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    int ok = 0;
    FAULT(facing(w, a, 1, &ok));
    if (!ok) return 0;
    put8(a, 0xD, 1);                                                   /* 0015D890 */
    put8(a, 5, 0xB);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x15);                                              /* 0015D8A4 */
    *result = 1;
    return 0;
}

/* Case 0x33 (0015D8A8..0015D9A4). */
static int action_33(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    if (w->world->d810C7D == 0) return 0;                              /* 0015D8B0 */
    int ok = 0;
    FAULT(facing(w, a, 1, &ok));                                       /* 0015D8BC */
    if (!ok) return 0;
    FAULT(trs(w, a));                                                  /* 0015D8D8 */
    FAULT(step_back(w, a));                                            /* 0015D914 */
    FAULT(trs(w, a));                                                  /* 0015D928 */
    put8(a, 0xD, 2);                                                   /* 0015D934 */
    put8(a, 5, 0xD);
    put8(a, 6, 0);                                                     /* 0015D950 */
    uint32_t b0[3];
    words(a, 0xB0, b0, 3);
    copy3(s->s38A0, b0);                                               /* 0015D94C */
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_10);                   /* 0015D980 */
    int r = 0;
    FAULT(classify(w, a, s->s38A0, 2, &r));                            /* 0015D97C */
    put8(a, 0x1F0, r == 0 ? 0x1B : 0x1C);                              /* 0015D990 / 0015D99C */
    *result = 1;
    return 0;
}

/* Case 0x3A (0015D9A8..0015DB2C). */
static int action_3a(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    uint32_t b0[4];
    words(a, 0xB0, b0, 4);
    copy3(s->s38A0, b0);                                               /* 0015D9B0 */
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_40);                   /* 0015D9E0 */
    int hit = 0;
    FAULT(w->segment_0019A570(c, b0, s->s38A0, 4, 0, &hit));           /* 0015D9E4 */
    if (hit != 0) {                                                    /* 0015D9EC */
        FAULT(record_ok(s, 0x0015D9FC));
        put8(a, 0x23B, s->record_bytes[0x1A]);                          /* 0015DA00 */
    }
    uint8_t attr = b8(a, 0x23B);                                       /* 0015DA04 */
    if (attr == 0x1E) {                                                /* 0015DA0C */
        put8(a, 5, 0x11);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0x20);
        put32(a, 0x254, em_ee_sub_bits(s->s31B0[1], F_20_5));          /* 0015DA44 */
        *result = 1;
        return 0;
    }
    if (attr != 0x34) return 0;                                        /* 0015DA4C */
    put32(a, 0x30C, s->record_word);                                   /* 0015DA64 */
    int ignored = 0;
    FAULT(refresh(w, a, &ignored));                                    /* 0015DA60 */
    FAULT(record_ok(s, 0x0015DA78));
    put32(a, 0x2E0, rec32(s, 0x34));                                   /* 0015DA80 */
    put32(a, 0x2E8, rec32(s, 0x3C));                                   /* 0015DA90 */
    int ok = 0;
    FAULT(facing(w, a, 2, &ok));                                       /* 0015DA8C */
    if (!ok) return 0;
    words(a, 0xB0, b0, 4);
    copy3(s->s38A0, b0);                                               /* 0015DAA4 */
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_40);                   /* 0015DAD4 */
    FAULT(w->segment_0019A570(c, b0, s->s38A0, 4, 0, &hit));           /* 0015DAD8 */
    if (hit == 0) {                                                    /* 0015DAE0 */
        *result = 1;
        return 0;
    }
    put8(a, 0x23B, 0x34);                                              /* 0015DAEC */
    put32(a, 0x254, em_ee_sub_bits(s->s31B0[1], F_20_5));              /* 0015DB0C */
    uint32_t mid[3];
    words(a, 0x290, mid, 3);
    int written = 0;
    FAULT(em_player_ladder_00199DB0(w->world, s, mid, &written));      /* 0015DB08 */
    if (written) put_words(a, 0x290, mid, 3);
    put8(a, 5, 0xF);                                                   /* 0015DB14 */
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x20);
    *result = 1;
    return 0;
}

/* Case 0x20 (0015DB30..0015DC28). */
static int action_20(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    if (w->world->d810C7C == 0) return 0;                              /* 0015DB38 */
    FAULT(center(w, s->s38A0));                                        /* 0015DB44 */
    copy3(s->s38B0, s->s38A0);                                         /* 0015DB58 */
    s->s38B0[1] = em_ee_add_bits(s->s38B0[1], F_40);                   /* 0015DB8C */
    int hit = 0;
    FAULT(w->segment_0019A570(c, s->s38A0, s->s38B0, 4, 0, &hit));     /* 0015DB90 */
    if (hit == 0) return 0;                                            /* 0015DB98 */
    FAULT(record_ok(s, 0x0015DBAC));
    if (s->record_bytes[0x1A] != 0x3C) return 0;                       /* 0015DBB0 */
    uint32_t angle = 0;
    FAULT(w->atan2_0011E620(c, em_ee_neg_bits(rec32(s, 0x3C)), rec32(s, 0x34), &angle));
    s->s3A20[0] = angle;                                               /* 0015DBCC */
    FAULT(w->wrap_001B1470(c, em_ee_add_bits(F_HALF_PI, s->s3A20[0]), &angle));
    s->s3A20[0] = angle;                                               /* 0015DBF0 */
    put32(a, 0x218, angle);                                            /* 0015DBF4 */
    put32(a, 0xB0, s->s38A0[0]);                                       /* 0015DC0C */
    put32(a, 0xB8, s->s38A0[2]);                                       /* 0015DC18 */
    put8(a, 5, 0x16);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x29);                                              /* 0015DC28 */
    *result = 1;
    return 0;
}

/* Case 0x3D (0015DC2C..0015DE98). */
static int action_3d(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    uint32_t yaw = w32(a, 0xC4);                                       /* 0015DC2C */
    int ok = 0;
    FAULT(facing(w, a, 0, &ok));                                       /* 0015DC34 */
    if (!ok) return 0;
    FAULT(trs(w, a));                                                  /* 0015DC50 */
    s->s38A0[0] = F_ZERO;
    s->s38A0[1] = F_10;
    s->s38A0[2] = F_10;
    s->s38A0[3] = F_ONE;
    uint32_t m[16];
    words(a, 0xD0, m, 16);
    FAULT(w->apply(c, s->s38B0, m, s->s38A0));                         /* 0015DC90 */
    int hit = 0;
    FAULT(w->move_0019AD00(c, a, s->s38B0, 7, &hit));                  /* 0015DCA4 */
    if (hit == 0) goto restore;                                        /* 0015DCAC */
    FAULT(record_ok(s, 0x0015DCD8));
    s->s38A0[0] = em_ee_add_bits(s->s31B0[0], em_ee_mul_bits(F_3_8, rec32(s, 0x24)));
    s->s38A0[2] = em_ee_add_bits(s->s31B0[2], em_ee_mul_bits(F_3_8, rec32(s, 0x2C)));
    s->s38A0[1] = s->s38B0[1];                                         /* 0015DD20 */
    s->s38A0[3] = F_ONE;                                               /* 0015DD2C */
    copy4(s->s38B0, s->s38A0);                                         /* 0015DD28 */
    s->s38B0[1] = em_ee_add_bits(s->s38B0[1], F_30);                   /* 0015DD5C */
    FAULT(w->segment_0019A570(c, s->s38A0, s->s38B0, 6, 0, &hit));     /* 0015DD60 */
    if (hit == 0) goto restore;                                        /* 0015DD68 */
    copy3(s->s38A0, s->s31B0);                                         /* 0015DD7C */
    s->s38B0[0] = F_ZERO;                                              /* 0015DD88 */
    s->s38B0[1] = F_ZERO;
    s->s38B0[2] = F_M10;                                               /* 0015DDB0 */
    s->s38B0[3] = F_ONE;
    s->s38A0[1] = em_ee_add_bits(s->s38A0[1], F_HALF);                 /* 0015DDDC */
    words(a, 0xD0, m, 16);
    FAULT(w->apply(c, s->s38C0, m, s->s38B0));                         /* 0015DDD8 */
    s->s38C0[1] = s->s38A0[1];                                         /* 0015DE08 */
    FAULT(w->sweep_0019AFE0(c, a, s->s38C0, s->s38A0, 6, &hit));       /* 0015DE04 */
    if (hit == 0) goto restore;                                        /* 0015DE0C */
    FAULT(record_ok(s, 0x0015DE2C));
    put32(a, 0x290, em_ee_add_bits(s->s31B0[0], em_ee_mul_bits(F_1_5, rec32(s, 0x24))));
    put32(a, 0x298, em_ee_add_bits(s->s31B0[2], em_ee_mul_bits(F_1_5, rec32(s, 0x2C))));
    uint32_t lo[3], hi[3];
    int corners = 0;
    FAULT(em_player_ladder_00199FA0(w->world, s, lo, hi, &corners));   /* 0015DE60 */
    if (!corners) {
        /* 0015DE68 reads the stack word 00199FA0 did not write. */
        if (!s->fault) s->fault = 0x0015DE68;
        return -1;
    }
    put32(a, 0x294, em_ee_sub_bits(hi[1], F_20_5));                    /* 0015DE84 */
    put8(a, 5, 0xA);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x14);                                              /* 0015DE94 */
    *result = 1;
    return 0;
restore:
    put32(a, 0xC4, yaw);                                               /* 0015DE98 */
    return 0;
}

static int use_action(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    *result = 0;
    int attr = 0;
    FAULT(refresh(w, a, &attr));                                       /* 0015D4D4 */
    if (attr == 0) return 0;                                           /* 0015D4DC */
    switch (b8(a, 0x23B)) {                                            /* 0015D4E4 */
    case 0x37: case 0x38: return action_ledge(w, a, result);
    case 0x32: return action_ladder(w, a, result);
    case 0x3B: return action_3b(w, a, result);
    case 0x33: return action_33(w, a, result);
    case 0x3A: return action_3a(w, a, result);
    case 0x20: return action_20(w, a, result);
    case 0x3D: return action_3d(w, a, result);
    default: return 0;                                                 /* 0015D548 */
    }
}

/* ---- 00182A70, 0017FC80, 00176DC0 ---------------------------------------- */

static int step_sound(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    int base = 0;
    FAULT(w->sound_base_00179B90(w->context, a, &base));               /* 00182A7C */
    return w->sound(w->context, a, (int)((uint32_t)base + 0x109u));    /* 00182A94 */
}

static int clip_by_2f1(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, float blend)
{
    int clip = 0;
    if (b8(a, 0x2F1) == 0)                                             /* 0017FC98 */
        FAULT(w->clip_001885D0(w->context, a, &clip));
    else
        FAULT(w->clip_001885F0(w->context, a, &clip));
    return w->request(w->context, a, clip, 0, blend);                  /* 0017FCB4 / 0017FCD8 */
}

/* One of 00176DC0's probes: 0x700038A4 = height, the point through the
 * 0x700036A0 matrix, 0019AD00(p, point, 6); 1 when bit 2 of its result is
 * set and 001762E0 returns nonzero. */
static int wall_probe(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, uint32_t height,
                      int *stop)
{
    EmPlayerLadderScratch *s = w->scratch;
    int hit = 0, wall = 0;
    *stop = 0;
    s->s38A0[1] = height;
    FAULT(w->apply(w->context, s->s38B0, s->s36A0, s->s38A0));
    FAULT(w->move_0019AD00(w->context, a, s->s38B0, 6, &hit));
    if (!(hit & 2)) return 0;                                          /* 00176E98 */
    FAULT(w->wall_001762E0(w->context, a, &wall));                     /* 00176EA0 */
    *stop = wall != 0;                                                 /* 00176EA8 */
    return 0;
}

static int wall_probes(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    put8(a, 0x314, 0);                                                 /* 00176DE0 */
    for (unsigned k = 0; k < 5; ++k) {                                 /* i = 3 .. 7 */
        FAULT(w->identity(c, s->s36A0));                               /* 00176DEC */
        uint32_t angle = 0;
        FAULT(w->wrap_001B1470(c, em_ee_add_bits(w32(a, 0xC4), kWallYaw[k]), &angle));
        uint32_t b0[4];
        FAULT(w->rotate_y(c, s->s36A0, s->s36A0, angle));              /* 00176E14 */
        words(a, 0xB0, b0, 4);
        FAULT(w->translate(c, s->s36A0, s->s36A0, b0));                /* 00176E2C */
        s->s38A0[0] = F_ZERO;
        s->s38A0[2] = F_5_5;
        s->s38A0[3] = F_ONE;
        int stop = 0;
        FAULT(wall_probe(w, a, F_4_01, &stop));                        /* 00176E78 */
        if (stop) return 0;
        FAULT(wall_probe(w, a, F_10, &stop));                          /* 00176ED0 */
        if (stop) return 0;
        FAULT(wall_probe(w, a, F_18, &stop));
        if (stop) return 0;
    }
    return 0;
}

/* ---- 00165B60 ------------------------------------------------------------ */

static int yaw_flip(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    uint32_t yaw = 0;
    FAULT(w->wrap_001B1470(w->context, em_ee_add_bits(F_PI, w32(a, 0xC4)), &yaw));
    put32(a, 0xC4, yaw);
    return 0;
}

/* The hand-off to state 0xC shared by cases 2 and 12 (00165E10.. and
 * 00166268..): +2F1 = 0, the D_002754D0[0] clip, 0017FC80(p, 16.0), then
 * +5 = 0xC, +6 = 0, +1F0 = 0x17. */
static int hand_off(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    put8(a, 0x2F1, 0);
    FAULT(w->request(w->context, a, w->world->d2754D0, 0, 0.0f));
    FAULT(clip_by_2f1(w, a, em_ee_float(F_16)));
    put8(a, 5, 0xC);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x17);
    return 0;
}

/* Case 2 when the clip ends (00165D34..00165E50). */
static int climb_on(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    if (b8(a, 0x1F0) == 0x16) FAULT(yaw_flip(w, a));                   /* 00165D3C */
    uint32_t m[16], angles[4];
    words(a, 0xD0, m, 16);
    FAULT(w->identity(c, m));                                          /* 00165D64 */
    put_words(a, 0xD0, m, 16);
    words(a, 0xC0, angles, 4);
    FAULT(w->euler(c, m, m, angles));                                  /* 00165D74 */
    put_words(a, 0xD0, m, 16);
    s->s38A0[0] = F_ZERO;
    s->s38A0[1] = F_M10_4;
    s->s38A0[2] = F_6_3;
    s->s38A0[3] = F_ZERO;                                              /* 00165DC0 */
    FAULT(w->apply(c, s->s38B0, m, s->s38A0));                         /* 00165DBC */
    uint32_t node[4], out[4];
    FAULT(w->node(c, node));                                           /* 00165DC4 */
    FAULT(w->vadd(c, out, node, s->s38B0));                            /* 00165DD8 */
    put_words(a, 0xB0, out, 4);
    put32(a, 0xBC, F_ONE);                                             /* 00165DEC */
    uint32_t b0[4];
    words(a, 0xB0, b0, 4);
    FAULT(w->translate(c, m, m, b0));                                  /* 00165DF0 */
    put_words(a, 0xD0, m, 16);
    if (b8(a, 0x1F0) == 0x16) put32(a, 0xB4, w32(a, 0x294));           /* 00165E0C */
    return hand_off(w, a);
}

/* Case 2, +1F0 = 0x16, sub-state 0 (00165E84..00165FD8). */
static int measure_drop(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    EmPlayerLadderScratch *s = w->scratch;
    void *c = w->context;
    if (!le(w32(a, 0x3C), F_25)) return 0;                             /* 00165E94 */
    uint32_t node[4];
    FAULT(w->node(c, node));
    copy3(s->s38A0, node);                                             /* 00165EB4 */
    FAULT(yaw_flip(w, a));                                             /* 00165EE8 */
    FAULT(trs(w, a));                                                  /* 00165EE4 */
    int r = 0;
    FAULT(classify(w, a, s->s38A0, 0, &r));                            /* 00165EF8 */
    if (r == 0) {
        uint32_t lo[3], hi[3];
        int corners = 0;
        FAULT(em_player_ladder_00199FA0(w->world, s, lo, hi, &corners));   /* 00165F0C */
        if (corners) {
            uint32_t drop = 0;
            FAULT(w->fabs_0011DF78(c, em_ee_sub_bits(hi[1], lo[1]), &drop));
            s->s3A20[0] = drop;                                        /* 00165F34 */
            s->s3A20[0] = em_ee_sub_bits(s->s3A20[0], F_16);           /* 00165F44 */
            for (;;) {                                                 /* 00165F50 */
                uint32_t next = em_ee_sub_bits(s->s3A20[0], F_3);
                if (next == s->s3A20[0]) {
                    /* The subtraction no longer moves the word: the
                     * original loops forever. */
                    if (!s->fault) s->fault = 0x00165F58;
                    return -1;
                }
                s->s3A20[0] = next;                                    /* 00165F6C */
                if (lt(next, F_3)) break;                              /* 00165F68 */
            }
            put8(a, 7, b8(a, 7) + 1);                                  /* 00165F8C */
            put32(a, 0x294, em_ee_sub_bits(w32(a, 0xB4), em_ee_add_bits(F_16, s->s3A20[0])));
            put32(a, 0x2E4, em_ee_div_bits(s->s3A20[0], em_ee_sub_bits(w32(a, 0x3C), F_ONE)));
        }
    }
    return yaw_flip(w, a);                                             /* 00165FCC */
}

/* The step sounds after case 2 (00165FEC..001661DC). */
static int climb_steps(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    static const uint32_t kLongStep[4] = { F_63, F_52, F_20, F_2 };
    uint32_t clock = w32(a, 0x3C);
    if (b8(a, 0x1F0) == 0x16) {                                        /* 00165FF4 */
        int16_t step = (int16_t)em_live_u16(a, 0x28);                   /* 00165FFC */
        if (step < 0 || step > 3) return 0;
        if (!le(clock, kLongStep[step])) return 0;
        em_live_set_u16(a, 0x28, (uint16_t)(step + 1));
        FAULT(step_sound(w, a));
        if (step == 3) return w->sound(w->context, a, 0x107);          /* 0016611C */
        return 0;
    }
    uint8_t step = b8(a, 7);                                           /* 0016612C */
    if (step == 0) {
        if (!le(clock, F_24)) return 0;                                /* 00166168 */
        put8(a, 7, step + 1);
        return step_sound(w, a);
    }
    if (step == 1) {
        if (!le(clock, F_2)) return 0;                                 /* 001661A0 */
        put8(a, 7, step + 1);
        FAULT(step_sound(w, a));
        return w->sound(w->context, a, 0x107);                         /* 001661D0 */
    }
    return 0;
}

static int ladder_entry(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    const EmPlayerLadderWorld *world = w->world;
    uint8_t st = b8(a, 6);                                             /* 00165B6C */
    switch (st) {
    case 0: {                                                          /* 00165BBC */
        uint32_t x = w32(a, 0xB0), y = w32(a, 0xB4), z = w32(a, 0xB8);
        if (world->area == 0xD && !lt(x, F_1010) && le(x, F_1030) && !lt(y, F_170) &&
            le(y, F_180) && !lt(z, F_830) && le(z, F_850)) {
            put8(a, 6, 0xA);                                           /* 00165C94 */
            put8(a, 7, 0);
            put8(a, 0x1F0, 0x15);
            break;
        }
        put8(a, 6, b8(a, 6) + 1);                                      /* 00165CB4 */
        put8(a, 7, 0);
        if (b8(a, 0x1F0) == 0x15) {                                    /* 00165CC0 */
            FAULT(w->request(w->context, a, 0xE3, 0, em_ee_float(F_8)));
        } else {
            FAULT(w->request(w->context, a, 0xE4, 0, em_ee_float(F_8)));
            em_live_set_u16(a, 0x28, 0);                               /* 00165D04 */
        }
        break;
    }
    case 1:                                                            /* 00165D08 */
        if (!(w32(a, 0x200) & 0x8000)) put8(a, 6, st + 1);
        break;
    case 2:                                                            /* 00165D24 */
        if (w32(a, 0x200) & 0x1000) {
            FAULT(climb_on(w, a));
        } else if (b8(a, 0x1F0) == 0x16) {                             /* 00165E5C */
            uint8_t sub = b8(a, 7);
            if (sub == 0) {
                FAULT(measure_drop(w, a));
            } else if (sub == 1) {                                     /* 00165FDC */
                put32(a, 0xB4, em_ee_sub_bits(w32(a, 0xB4), w32(a, 0x2E4)));
            }
        }
        FAULT(climb_steps(w, a));
        break;
    case 0xA:                                                          /* 001661E4 */
        put8(a, 6, st + 1);
        FAULT(w->request(w->context, a, 0x70, 0, em_ee_float(F_ONE)));
        break;
    case 0xB:                                                          /* 00166204 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 6, st + 1);
            put32(a, 0xB0, F_1021_9);
            put32(a, 0xB4, F_187_2);
            put32(a, 0xB8, F_836_3);
            FAULT(w->request(w->context, a, 0xFE, 0, 0.0f));
        }
        break;
    case 0xC:                                                          /* 00166258 */
        if (w32(a, 0x200) & 0x1000) FAULT(hand_off(w, a));
        break;
    default:
        break;
    }
    if (world->area == 2) FAULT(wall_probes(w, a));                    /* 001662B0 */
    return 0;
}

/* ---- entry points --------------------------------------------------------- */

int em_player_ladder_0015D4C0(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    if (!em_player_ladder_workers_bound(w) || !a || !result) return -1;
    return use_action(w, a, result);
}

int em_player_ladder_00176F90(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int *result)
{
    if (!em_player_ladder_workers_bound(w) || !a || !result) return -1;
    return refresh(w, a, result);
}

int em_player_ladder_00177030(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, int mode,
                              int *result)
{
    if (!em_player_ladder_workers_bound(w) || !a || !result) return -1;
    return facing(w, a, mode, result);
}

int em_player_ladder_00180300(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a,
                              const uint32_t v[4], int check, int *result)
{
    if (!em_player_ladder_workers_bound(w) || !a || !v || !result) return -1;
    return classify(w, a, v, check, result);
}

int em_player_ladder_00165B60(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    if (!em_player_ladder_workers_bound(w) || !a) return -1;
    return ladder_entry(w, a);
}

int em_player_ladder_00176DC0(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    if (!em_player_ladder_workers_bound(w) || !a) return -1;
    return wall_probes(w, a);
}

int em_player_ladder_0017FC80(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a, float blend)
{
    if (!em_player_ladder_workers_bound(w) || !a) return -1;
    return clip_by_2f1(w, a, blend);
}

int em_player_ladder_00182A70(const EmPlayerLadderWorkers *w, EmPlayerLiveActor *a)
{
    if (!em_player_ladder_workers_bound(w) || !a) return -1;
    return step_sound(w, a);
}

int em_player_ladder_state_b(void *workers, EmPlayerLiveActor *actor)
{
    return em_player_ladder_00165B60((const EmPlayerLadderWorkers *)workers, actor);
}

/* ---- 001B61C0 ------------------------------------------------------------ */

int em_player_rumble_001B61C0(const EmPlayerRumble *r, int big, int small, int duration, int force)
{
    if (!r || !r->pad || !r->enable || !r->mode || !r->actuator) return -1;
    EmPlayerRumblePad *p = r->pad;
    if (*r->enable == 0) return 0;                                     /* 001B61D8 */
    if (p->ready == 0) return 0;                                       /* 001B61E4 */
    if (*r->mode == 2) return 0;                                       /* 001B61F4 */
    if (force == 0 && p->active != 0) return 0;                        /* 001B61FC / 001B6208 */
    p->active = 1;                                                     /* 001B6214 */
    p->duration = (uint16_t)duration;                                  /* 001B621C */
    if (big != 0) p->act[0] = 1;                                       /* 001B6220 */
    p->act[1] = (uint8_t)small;                                        /* 001B6224 */
    return r->actuator(r->context, p->port, p->slot, p->act);          /* 001B6230 */
}

int em_player_rumble_worker(void *rumble, int big, int small, int duration, int force)
{
    return em_player_rumble_001B61C0((const EmPlayerRumble *)rumble, big, small, duration, force);
}
