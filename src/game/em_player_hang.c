/* em_player_hang.c - the player's ledge hang, state +5 = 9 (see em_player_hang.h).
 *
 * Read from the original instructions of 001647D0 (the decomp's C is a
 * NEARMISS; docs/PLAYER_HANG.md lists what the instructions settle), of the
 * asm-word leaf 0017F240 and of the VU0 leaf 001028B8. Every comment address
 * is the original instruction the line translates. EE COP1 and VU0 arithmetic
 * goes through em_ee_float.h on raw binary32 bits; loads and stores of floats
 * the original only moves (float loads, stores and moves) copy the bits. */
#include "game/em_player_hang.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Binary32 constants as the original materializes them (upper-half immediate, low half OR'd in). */
#define K_0       UINT32_C(0x00000000)
#define K_0_5     UINT32_C(0x3F000000)
#define K_1       UINT32_C(0x3F800000)
#define K_2       UINT32_C(0x40000000)
#define K_3       UINT32_C(0x40400000)
#define K_3_5     UINT32_C(0x40600000)
#define K_4       UINT32_C(0x40800000)
#define K_5       UINT32_C(0x40A00000)
#define K_6       UINT32_C(0x40C00000)
#define K_8       UINT32_C(0x41000000)
#define K_9       UINT32_C(0x41100000)
#define K_10_5    UINT32_C(0x41280000)
#define K_16      UINT32_C(0x41800000)
#define K_18      UINT32_C(0x41900000)
#define K_20      UINT32_C(0x41A00000)
#define K_25      UINT32_C(0x41C80000)
#define K_115     UINT32_C(0x42E60000)
#define K_270     UINT32_C(0x43870000)
#define K_300     UINT32_C(0x43960000)
#define K_340     UINT32_C(0x43AA0000)
#define K_M0_2    UINT32_C(0xBE4CCCCD)
#define K_M2      UINT32_C(0xC0000000)
#define K_M5      UINT32_C(0xC0A00000)
#define K_M9      UINT32_C(0xC1100000)

/* Original tables (read-only data of the pinned ELF; the oracle checks every
 * word against the user's ELF). Indexed by the gait byte +23F. */
static const uint32_t kD002485E0[4] = { 0x00000000u, 0x3CF5C28Fu, 0x3D75C28Fu, 0x3DF5C28Fu };
static const uint32_t kD002485F0[4] = { 0x00000000u, 0x3F4CCCCDu, 0x3F800000u, 0x3FA00000u };
static const uint32_t kD00248600[4] = { 0x00000000u, 0x3F000000u, 0x3F4CCCCDu, 0x3F99999Au };
/* D_00275498 (.sdata words), indexed by the side byte +2F1. */
static const int32_t kD00275498[2] = { 2, 3 };

static uint8_t u8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(EmPlayerLiveActor *a, unsigned at, uint8_t v) { em_live_set_u8(a, at, v); }
static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static float fl(uint32_t bits) { return em_ee_float(bits); }

/* ---- 0017F240 ------------------------------------------------------------ */

int em_player_hang_0017F240(EmPlayerLiveActor *a, int arg)
{
    /* 0017F240..0017F270: +224 == 0 and +22C == 0 (EE float compares against 0.0)
     * with +F bit 1 clear returns 0 at once (0017F27C delay slot). */
    if (em_ee_c_eq_bits(w32(a, 0x224), K_0) && em_ee_c_eq_bits(w32(a, 0x22C), K_0) &&
        !(u8(a, 0xF) & 2))
        return 0;
    /* 0017F280: arg == 0, or (0017F2AC) the non-looping clip end +200 & 0x1000. */
    if (arg == 0 || (w32(a, 0x200) & 0x1000u)) {
        uint8_t state = u8(a, 5);                    /* 0017F288 / 0017F2BC */
        set8(a, 0x302, state);                       /* 0017F298 */
        set8(a, 4, 2);                               /* 0017F29C */
        set8(a, 5, 6);                               /* 0017F2A0 */
        set8(a, 6, 0);                               /* 0017F2A8 (delay slot) */
        return 1;                                    /* 0017F294: $v0 = 1 */
    }
    /* 0017F2E0: +3C < 3.0 returns 0 untouched; else +204 = +3C - 2.0. */
    uint32_t clock = w32(a, 0x3C);
    if (!em_ee_c_lt_bits(clock, K_3))
        set32(a, 0x204, em_ee_sub_bits(clock, K_2));  /* 0017F30C / 0017F310 */
    return 0;
}

/* ---- 001028B8 ------------------------------------------------------------ */

int em_player_hang_vadd(void *context, float out[4], const float a[4], const float b[4])
{
    (void)context;
    uint32_t x[4], y[4], z[4] = { 0, 0, 0, 0 };
    memcpy(x, a, sizeof x);
    memcpy(y, b, sizeof y);
    /* 001028B8: out.xyzw = a.xyzw + b.xyzw, one VU0 add over all four lanes */
    if (em_vu_vec_bits(EM_VU_ADD, 15, EM_VU_NO_BC, x, y, 0, NULL, z) != EM_EE_FLOAT_OK) return -1;
    memcpy(out, z, sizeof z);
    return 0;
}

/* ---- 001647D0 ------------------------------------------------------------ */

typedef struct HangCall {
    const EmPlayerHangWorkers *w;
    EmPlayerLiveActor *a;
    EmPlayerHangScene scene;
} HangCall;

static int node_word(HangCall *c, int node, unsigned offset, uint32_t *bits)
{
    float value = 0.0f;
    FAULT(c->w->node(c->w->context, node, offset, &value));
    *bits = em_ee_bits(value);
    return 0;
}

static int request(HangCall *c, int clip, uint32_t blend)
{
    return c->w->request(c->w->context, c->a, clip, 0, fl(blend));
}

static int sound(HangCall *c, int id)
{
    return c->w->sound(c->w->context, c->a, id, 0, fl(K_300));
}

static int hang_clear(HangCall *c, int *result)
{
    return c->w->hang_clear(c->w->context, c->a, result);
}

/* The steering input the hold states share: under the scripted takeover
 * +24C = 0 and +23F = 2 (001649D8 / 001649E0), else 00174FD0 (001649E8). */
static int steer(HangCall *c)
{
    if (c->scene.scripted) {
        set32(c->a, 0x24C, 0);
        set8(c->a, 0x23F, 2);
        return 0;
    }
    return c->w->steer_input(c->w->context, c->a);
}

/* 00188550 then 001749A0(p, clip, 0, 16.0) (001651A0.., 00165870.., ...). */
static int row_request(HangCall *c)
{
    int clip = 0;
    FAULT(c->w->clip_row(c->w->context, c->a, &clip));
    return request(c, clip, K_16);
}

/* The +2F1 index into D_00275498 (the original reads the word at
 * D_00275498 + 4 * +2F1; outside the two-word table it would read
 * neighbouring data, which the native refuses). */
static int side_word(const EmPlayerLiveActor *a, int32_t *word)
{
    uint8_t side = u8(a, 0x2F1);
    if (side > 1) return -1;
    *word = kD00275498[side];
    return 0;
}

static int gait(const EmPlayerLiveActor *a, unsigned *index)
{
    uint8_t g = u8(a, 0x23F);
    if (g > 3) return -1;
    *index = g;
    return 0;
}

/* The +24C test of sub-states 0x20 / 0x2A (00165334.. / 00165AD0..):
 * 1 when the steering quadrant differs from D_00275498[+2F1]. */
static int steer_left_side(HangCall *c, int *differs)
{
    int32_t want = 0;
    FAULT(side_word(c->a, &want));
    *differs = (int32_t)w32(c->a, 0x24C) != want;
    return 0;
}

static void hang_drop(EmPlayerLiveActor *a)
{
    set8(a, 6, 0xA);                                 /* 001649B4 et al. */
    set8(a, 0x1F0, 0x13);                            /* 001649C0 et al. */
}

/* 0017E7C0's result mapping of sub-state 1 (00164AFC..00164B9C). */
static void side_result(EmPlayerLiveActor *a, int r)
{
    if (r == 1) set8(a, 6, 0x14);
    else if (r == 2) set8(a, 6, 0x1E);
    else if (r == 0xA) set8(a, 6, 0x28);
}

static int hold(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0;
    /* 00164990: 0017F240(p, 0). */
    if (em_player_hang_0017F240(a, 0) != 0) return 0;
    FAULT(hang_clear(c, &r));                        /* 001649A0 */
    if (r != 0) { hang_drop(a); return 0; }
    FAULT(steer(c));                                 /* 001649C4..001649EC */
    int32_t mode = (int32_t)w32(a, 0x24C);           /* 001649F0 */
    if (mode == 0) {
        if (u8(a, 0xD) != 1) {                       /* 001649FC */
            int ahead = 0, above = 0;
            FAULT(w->ledge_ahead(w->context, a, &ahead));   /* 00164A10 */
            if (ahead == 0) FAULT(w->ledge_above(w->context, a, &above)); /* 00164A20 */
            if (ahead == 0 && above == 0) {
                unsigned g = 0;
                set8(a, 6, (uint8_t)(u8(a, 6) + 1));  /* 00164A30..00164A44 */
                set8(a, 0x1F0, 0x11);                 /* 00164A48 */
                FAULT(gait(a, &g));                   /* 00164A4C */
                set32(a, 0x26C, kD00248600[g]);       /* 00164A74 (delay slot) */
                FAULT(request(c, 0x7C, K_5));         /* 00164A70 */
                set8(a, 7, 0);                        /* 00164A7C */
                return 0;
            }
            if (c->scene.scripted) hang_drop(a);      /* 00164A80 */
        }
    } else if (mode == 1) {
        if (c->scene.pad & c->scene.use_mask) hang_drop(a);   /* 00164AB0.. */
    } else if (mode == 2 || mode == 3) {
        int side = mode == 2 ? 0 : 1;
        set8(a, 0x2F1, (uint8_t)side);               /* 00164AF8 / 00164B50 */
        FAULT(w->ledge_side(w->context, a, side, &r));  /* 00164AF4 / 00164B54 */
        side_result(a, r);
    }
    set8(a, 7, 0);                                   /* 00164BA0 */
    return 0;
}

static int state_0(HangCall *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    set8(a, 6, (uint8_t)(st + 1));                   /* 001648D8 */
    set8(a, 0x23C, 0);
    set8(a, 0x23D, 0);
    set8(a, 0x23E, 0);
    set8(a, 0x2F2, 0);
    set8(a, 0x2F1, 0);                               /* 001648EC */
    set32(a, 0x38, 0);                               /* 001648F4 (delay slot) */
    FAULT(w->aim_track(w->context, a));              /* 001648F0: 00182250 */
    set8(a, 0x316, 0);                               /* 001648F8 */
    if (c->scene.area == 0x11 && u8(a, 0xD) == 0) {  /* 00164908 / 00164914 */
        /* The spad temporaries 0x70003A20 / 0x70003A28 / 0x70003A2C are
         * kept in locals (docs/PLAYER_HANG.md, limits). */
        uint32_t x = em_ee_sub_bits(w32(a, 0xB0), K_340);    /* 00164938 */
        uint32_t z = em_ee_sub_bits(w32(a, 0xB8), K_270);    /* 0016494C */
        uint32_t acc = em_ee_mula_bits(x, x);                /* 00164958 */
        uint32_t sum = em_ee_madd_bits(acc, z, z);           /* 00164964 */
        uint32_t d = em_ee_bits(w->sqrt(w->context, fl(sum))); /* 00164960: 0011E748 */
        if (em_ee_c_le_bits(d, K_115)) set8(a, 0x316, 1);    /* 00164974..00164988 */
    }
    return hold(c);                                  /* falls into 00164990 */
}

static int state_3(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0;
    if (w32(a, 0x200) & 0x1000u) {                   /* 00164BE0 */
        uint32_t node = 0;
        set8(a, 0x25F, 0);                           /* 00164BF4 (delay slot) */
        FAULT(w->skeleton(w->context, a));           /* 00164BF0: 001C68C0 */
        FAULT(node_word(c, 1, 0xC4, &node));         /* 00164C14 / 00164C18 */
        set32(a, 0xB4, em_ee_sub_bits(node, K_10_5)); /* 00164C1C / 00164C20 */
        FAULT(node_word(c, 1, 0x8, &node));          /* 00164C24..00164C2C */
        set32(a, 0x38, em_ee_add_bits(K_1, node));   /* 00164C30 / 00164C38 */
        FAULT(w->translate(w->context, a, 1));       /* 00164C34: 00178B90 */
        set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), K_M0_2)); /* 00164C50 / 00164C5C */
        FAULT(w->floor(w->context, a, 1, &r));       /* 00164C58: 00175900 */
        FAULT(request(c, 0x8C, K_0));                /* 00164C6C */
        if (u8(a, 0x23B) == 0x39) {                  /* 00164C74 */
            /* 00164C84..00164CBC: (0, 0, -5, 0) through the +D0 matrix,
             * then +B0 += it in all four lanes (001028B8). */
            const uint32_t local[4] = { K_0, K_0, K_M5, K_0 };
            float in[4], matrix[16], push[4], position[4], moved[4];
            memcpy(in, local, sizeof in);
            memcpy(matrix, a->bytes + 0xD0, sizeof matrix);
            FAULT(w->transform(w->context, push, matrix, in));      /* 00164CB8 */
            memcpy(position, a->bytes + 0xB0, sizeof position);
            FAULT(w->vadd(w->context, moved, position, push));      /* 00164CCC */
            memcpy(a->bytes + 0xB0, moved, sizeof moved);
            set8(a, 5, 7);                           /* 00164CD8 */
            set8(a, 6, 0);                           /* 00164CE0 */
            set8(a, 0x1F0, 0xD);                     /* 00164CE4 */
            set8(a, 0x25F, 2);                       /* 00164CEC */
            set32(a, 0x2EC, 0);                      /* 00164CF0 */
            set32(a, 0x2F4, w32(a, 0xB4));           /* 00164CF4 / 00164CFC */
            return 0;
        }
        float at[4];
        memcpy(at, a->bytes + 0xB0, sizeof at);
        FAULT(w->column(w->context, a, at, 1, fl(K_18), &r)); /* 00164D10: 001760C0 */
        if (r != 0) {
            set8(a, 0x236, 1);                       /* 00164D24 */
            set8(a, 0x235, (uint8_t)(u8(a, 0x235) | 2));  /* 00164D28..00164D30 */
        }
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));         /* 00164D34..00164D3C */
        set32(a, 0x2EC, 0);                          /* 00164D44 */
        return 0;
    }
    if (em_ee_c_le_bits(w32(a, 0x3C), K_25)) {       /* 00164D48..00164D60 */
        set32(a, 0x204, K_0_5);                      /* 00164D70 */
        return 0;
    }
    FAULT(hang_clear(c, &r));                        /* 00164D74 */
    if (r != 0) { hang_drop(a); return 0; }
    FAULT(steer(c));                                 /* 00164D98..00164DC0 */
    if (w32(a, 0x24C) == 0) {                        /* 00164DC4 */
        unsigned g = 0;
        FAULT(gait(a, &g));                          /* 00164DD0 */
        uint32_t held = w32(a, 0x26C);               /* 00164DDC */
        uint32_t floor = kD00248600[g];              /* 00164DE8 */
        /* 00164DEC: +26C <= table stores the table value (the branch-likely delay-slot float copy),
         * else stores back the loaded bits. */
        set32(a, 0x26C, em_ee_c_le_bits(held, floor) ? floor : held);
    }
    set32(a, 0x204, w32(a, 0x26C));                  /* 00164E0C / 00164E14 */
    return 0;
}

static int state_4(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0;
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), K_M0_2));  /* 00164E2C / 00164E34 */
    FAULT(w->floor(w->context, a, 1, &r));           /* 00164E30: 00175900 */
    if (r == 0) return w->fall(w->context, a);       /* 00164EA0: 001796C0 */
    FAULT(w->land_sound(w->context, a, 1));          /* 00164E44: 00182870 */
    FAULT(w->heading(w->context, a, 0));             /* 00164E50: 00174AC0 */
    if (u8(a, 0x23F) >= 2) {                         /* 00164E58 / 00164E5C */
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));         /* 00164E68..00164E7C */
        return w->reentry(w->context, a, 1);         /* 00164E78: 0017C440 */
    }
    set8(a, 0x25C, 0);                               /* 00164E90 (delay slot) */
    return w->handoff(w->context, a);                /* 00164E8C: 0017C540 */
}

static int state_B(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    uint32_t flags = w32(a, 0x200);                  /* 00164F10 */
    if (flags & 0x1000u) {
        set8(a, 5, 7);                               /* 00164F24 */
        set8(a, 6, 0);                               /* 00164F2C */
        set8(a, 0x1F0, 0xD);                         /* 00164F34 */
        return 0;
    }
    if (flags & 0x8000u) return 0;                   /* 00164F3C */
    uint32_t node = 0;
    FAULT(node_word(c, 0, 0x8, &node));              /* 00164F44..00164F54 */
    set32(a, 0x38, em_ee_sub_bits(node, w32(a, 0x21C)));    /* 00164F58 / 00164F5C */
    FAULT(node_word(c, 0, 0x8, &node));              /* 00164F60..00164F68 */
    set32(a, 0x21C, node);                           /* 00164F70 (delay slot) */
    FAULT(w->translate(w->context, a, 1));           /* 00164F6C: 00178B90 */
    FAULT(node_word(c, 0, 0x4, &node));              /* 00164F74..00164F80 */
    set32(a, 0x2EC, em_ee_sub_bits(node, w32(a, 0x2E4)));   /* 00164F84 / 00164F88 */
    FAULT(node_word(c, 0, 0x4, &node));              /* 00164F8C..00164F94 */
    set32(a, 0x2E4, node);                           /* 00164F98 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2EC)));  /* 00164FA4 / 00164FAC */
    return 0;
}

static int state_14(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0;
    if (em_player_hang_0017F240(a, 0) != 0) return 0;      /* 00164FB4 */
    FAULT(hang_clear(c, &r));                        /* 00164FC4 */
    if (r != 0) { hang_drop(a); return 0; }
    uint8_t sub = u8(a, 7);                          /* 00164FE8 */
    if (sub == 0) {
        set8(a, 7, (uint8_t)(sub + 1));              /* 00165018 */
        return w->clip_DF70(w->context, a, u8(a, 0x2F1), fl(K_8));    /* 00165028 */
    }
    if (sub == 1) {
        if (!(w32(a, 0x200) & 0x8000u)) set8(a, 7, (uint8_t)(sub + 1));  /* 00165038.. */
        return 0;
    }
    if (sub != 2) return 0;
    FAULT(w->steer_input(w->context, a));            /* 00165058: 00174FD0 */
    int32_t want = 0;
    FAULT(side_word(a, &want));                      /* 00165060..00165074 */
    if ((int32_t)w32(a, 0x24C) == want) {            /* 00165078 */
        FAULT(w->ledge_side(w->context, a, u8(a, 0x2F1), &r));   /* 00165084: 0017E7C0 */
        if (r == 1) {
            unsigned g = 0;
            FAULT(gait(a, &g));                      /* 001650A4 / 001650E4 */
            if (u8(a, 0x2F1) == 0)                   /* 00165098 */
                set32(a, 0x38, em_ee_neg_bits(kD002485E0[g]));    /* 001650C4 / 001650C8 */
            else
                set32(a, 0x38, kD002485E0[g]);       /* 00165104 */
            FAULT(gait(a, &g));                      /* 001650CC / 00165108 */
            set32(a, 0x204, kD002485F0[g]);          /* 001650E0 / 00165118 */
            uint32_t cosine = em_ee_bits(w->cosine(w->context, fl(w32(a, 0xC4))));  /* 0016511C */
            set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), em_ee_mul_bits(w32(a, 0x38), cosine))); /* 0016512C..00165134 */
            uint32_t sine = em_ee_bits(w->sine(w->context, fl(w32(a, 0xC4))));      /* 00165138 */
            set32(a, 0xB8, em_ee_sub_bits(w32(a, 0xB8), em_ee_mul_bits(w32(a, 0x38), sine)));   /* 0016514C..00165158 */
            FAULT(w->aim_track(w->context, a));      /* 00165154: 00182250 */
        } else if (r == 2) {
            set8(a, 6, 0x1E);                        /* 00165174 */
            set8(a, 7, 0);                           /* 0016517C */
        } else if (r == 0xA) {
            set8(a, 6, 0x28);                        /* 00165190 */
            set8(a, 7, 0);                           /* 00165198 */
        } else {
            set8(a, 6, 0);                           /* 001651A4 (delay slot) */
            FAULT(row_request(c));                   /* 001651A0 / 001651B8 */
            return w->sound_100(w->context, a);      /* 001651C0: 00182AF0 */
        }
        if (w32(a, 0x200) & 0x1000u)                 /* 00165204 / 00165208 */
            return w->sound_100(w->context, a);      /* 00165214 */
        return 0;
    }
    set8(a, 6, 0);                                   /* 001651D8 (delay slot) */
    FAULT(row_request(c));                           /* 001651D4 / 001651EC */
    return w->sound_100(w->context, a);              /* 001651F4 */
}

/* 0x1E / 0x28: 0017F240 == 0 steps +6 and requests 0017DFB0(p, +2F1, 4.0). */
static int turn_start(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    if (em_player_hang_0017F240(a, 0) != 0) return 0;      /* 00165228 / 0016589C */
    set8(a, 6, (uint8_t)(u8(a, 6) + 1));                   /* 00165238..00165248 */
    return c->w->clip_DFB0(c->w->context, a, u8(a, 0x2F1), fl(K_4));    /* 00165250 */
}

/* 0x1F / 0x29: at the clip end, step +6 and request 0017E0D0(p, +2F1, 1.0). */
static int turn_end(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    if (em_player_hang_0017F240(a, 0) != 0) return 0;      /* 00165264 / 001658D8 */
    if (!(w32(a, 0x200) & 0x1000u)) return 0;              /* 00165274..0016527C */
    set8(a, 6, (uint8_t)(u8(a, 6) + 1));                   /* 00165284..00165294 */
    return c->w->clip_E0D0(c->w->context, a, u8(a, 0x2F1), fl(K_1));    /* 0016529C */
}

static int state_20(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0, differs = 0;
    if (em_player_hang_0017F240(a, 0) != 0) return 0;      /* 001652B0 */
    if (c->scene.pad & c->scene.use_mask) {                /* 001652C0..001652D4 */
        set8(a, 6, (uint8_t)(u8(a, 6) + 1));               /* 001652DC..001652EC */
        FAULT(w->clip_E1D0(w->context, a, u8(a, 0x2F1), fl(K_1)));  /* 001652F4 */
        set8(a, 0x1F0, 0x12);                              /* 00165304 */
        return 0;
    }
    FAULT(hang_clear(c, &r));                        /* 0016530C */
    if (r != 0) { hang_drop(a); return 0; }
    FAULT(w->steer_input(w->context, a));            /* 00165334 */
    FAULT(steer_left_side(c, &differs));             /* 0016533C..00165354 */
    if (!differs) return 0;
    set8(a, 6, 0x27);                                /* 00165360 */
    return w->clip_E150(w->context, a, u8(a, 0x2F1), fl(K_1));    /* 00165370 */
}

static int state_21(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    uint8_t cling = u8(a, 0x315);                    /* 00165380 */
    if (cling == 0) {
        if (!em_ee_c_le_bits(w32(a, 0x3C), K_6)) return 0;    /* 0016538C..001653A4, 001653AC */
    } else if (!(w32(a, 0x200) & 0x1000u)) {
        return 0;                                    /* 001653B4..001653BC */
    }
    set8(a, 6, (uint8_t)(u8(a, 6) + 1));             /* 001653C4..001653DC */
    FAULT(sound(c, 0x187));                          /* 001653E0 */
    set32(a, 0x2F4, w32(a, 0x2E0));                  /* 001653E8 / 001653F8 */
    set32(a, 0x2F8, w32(a, 0x2E8));                  /* 001653FC / 00165400 */
    set32(a, 0x258, w32(a, 0x2E4));                  /* 00165404 / 00165408 */
    em_live_set_u16(a, 0x28, 8);                     /* 0016540C */
    set32(a, 0x2E0, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F4), w32(a, 0xB0)), K_8)); /* 00165418..00165420 */
    set32(a, 0x2E8, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x2F8), w32(a, 0xB8)), K_8)); /* 0016542C..00165434 */
    set32(a, 0x2E4, em_ee_div_bits(em_ee_sub_bits(w32(a, 0x258), w32(a, 0xB4)), K_8)); /* 00165440..00165448 */
    uint32_t error = em_ee_sub_bits(w32(a, 0x218), w32(a, 0xC4));  /* 00165458 */
    uint32_t t = em_ee_bits(w->wrap(w->context, fl(error)));        /* 00165454: 001B1470 */
    /* 0x70003A20 = t (00165470) is kept local (docs/PLAYER_HANG.md, limits). */
    if (em_ee_c_lt_bits(t, K_0))                     /* 00165464 */
        set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(t), K_8));    /* 00165494..001654A8 */
    else
        set32(a, 0x26C, em_ee_div_bits(t, K_8));     /* 00165480 / 00165490 */
    uint8_t variant = u8(a, 0x1F1);                  /* 001654AC */
    if (variant == 1) return request(c, 0xE5, K_8);  /* 001654CC */
    if (variant == 6) return request(c, 0x96, K_8);  /* 001654F8 */
    if (variant == 2) {
        if (u8(a, 0x315) == 0)                       /* 00165514 */
            return w->clip_E150(w->context, a, u8(a, 0x2F1), fl(K_8));     /* 0016552C */
        /* 00165550: $a1 = $v1 - +2F1 with $v1 = 1 (001654B0). */
        return w->clip_E150(w->context, a, 1 - (int)u8(a, 0x2F1), fl(K_8));
    }
    if (variant == 5) set8(a, 0x315, 0);             /* 00165568 */
    return w->clip_E150(w->context, a, u8(a, 0x2F1), fl(K_8));    /* 00165578 / 00165594 */
}

static int state_22(HangCall *c, uint8_t st)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    if ((int16_t)em_live_u16(a, 0x28) == 0) {        /* 001655A4 */
        set8(a, 6, (uint8_t)(st + 1));               /* 001655B4 */
        set32(a, 0xB0, w32(a, 0x2F4));               /* 001655C0 */
        set32(a, 0xB8, w32(a, 0x2F8));               /* 001655C8 */
        set32(a, 0xB4, w32(a, 0x258));               /* 001655D0 */
        set32(a, 0xC4, w32(a, 0x218));               /* 001655D8 */
        uint8_t variant = u8(a, 0x1F1);              /* 001655DC */
        if (variant == 1) return w->sound_109(w->context, a);   /* 001655E8: 00182A70 */
        if (variant == 6) return sound(c, 0x119);    /* 00165610 */
        return sound(c, 0xFF);                       /* 00165638 / 00165660 / 0016567C */
    }
    set32(a, 0xB0, em_ee_add_bits(w32(a, 0xB0), w32(a, 0x2E0)));   /* 00165694 / 00165698 */
    set32(a, 0xB8, em_ee_add_bits(w32(a, 0xB8), w32(a, 0x2E8)));   /* 001656A4 / 001656A8 */
    set32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2E4)));   /* 001656B4 / 001656B8 */
    float heading = w->approach(w->context, fl(w32(a, 0x218)), fl(w32(a, 0xC4)),
                                fl(w32(a, 0x26C)));  /* 001656C4: 001B12B0 */
    set32(a, 0xC4, em_ee_bits(heading));             /* 001656CC */
    em_live_set_u16(a, 0x28, (uint16_t)(em_live_u16(a, 0x28) - 1));   /* 001656D0..001656DC */
    return 0;
}

static int state_23(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    if (!(w32(a, 0x200) & 0x1000u)) return 0;        /* 001656E0..001656E8 */
    uint8_t variant = u8(a, 0x1F1);                  /* 001656F0 */
    if (variant == 1) {
        FAULT(w->sound_109(w->context, a));          /* 00165700: 00182A70 */
        set8(a, 5, 0xC);                             /* 0016570C */
        set8(a, 6, 0);                               /* 00165714 */
        set8(a, 0x1F0, 0x17);                        /* 00165718 */
        set8(a, 0x2F1, 0);                           /* 0016572C (delay slot) */
        return w->clip_FC80(w->context, a, fl(K_16));      /* 00165728: 0017FC80 */
    }
    if (variant == 6) {
        FAULT(sound(c, 0x119));                      /* 00165750 */
        set8(a, 5, 0xE);                             /* 0016575C */
        set8(a, 6, 0);                               /* 00165764 */
        set8(a, 0x1F0, 0x1D);                        /* 00165768 */
        set8(a, 0xD, 2);                             /* 00165770 */
        set8(a, 0x2F1, 0);                           /* 00165784 (delay slot) */
        return w->clip_FF80(w->context, a, fl(K_16));      /* 00165780: 0017FF80 */
    }
    if (variant == 5) {
        set8(a, 5, 0x18);                            /* 001657E8 */
        set8(a, 6, 0);                               /* 001657F0 */
        set8(a, 0x1F0, 0x2C);                        /* 001657F4 */
        set8(a, 0x1F1, 0);                           /* 001657F8 */
        set8(a, 0xD, 2);                             /* 00165800 (delay slot) */
        return row_request(c);                       /* 001657FC / 00165814 */
    }
    /* variant 2 (0016579C..001657C8) and every other value (00165828..00165850). */
    set8(a, 5, 9);
    set8(a, 6, 0);
    set8(a, 0x1F0, 0x10);
    set8(a, 0xD, 0);
    return row_request(c);
}

/* 0x27 / 0x31: at the clip end, +6 = 0 and the hang row clip. */
static int turn_back(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    if (!(w32(a, 0x200) & 0x1000u)) return 0;        /* 00165860 / 00165B1C */
    set8(a, 6, 0);                                   /* 00165874 / 00165B30 (delay slot) */
    return row_request(c);
}

static int state_2A(HangCall *c)
{
    EmPlayerLiveActor *a = c->a;
    const EmPlayerHangWorkers *w = c->w;
    int r = 0, differs = 0;
    if (em_player_hang_0017F240(a, 0) != 0) return 0;      /* 00165924 */
    if (c->scene.pad & c->scene.use_mask) {                /* 00165934..00165948 */
        /* 0016595C..00165A04: spad A0 = (x, 20, -2, 1), B0 = (x, 20, 3.5, 1),
         * x = -9 for +2F1 == 0, else 9. */
        uint32_t x = u8(a, 0x2F1) == 0 ? K_M9 : K_9;       /* 00165950 */
        const uint32_t near[4] = { x, K_20, K_M2, K_1 };
        const uint32_t far[4] = { x, K_20, K_3_5, K_1 };
        float in_near[4], in_far[4], matrix[16], from[4], to[4];
        memcpy(in_near, near, sizeof in_near);
        memcpy(in_far, far, sizeof in_far);
        memcpy(matrix, a->bytes + 0xD0, sizeof matrix);
        FAULT(w->transform(w->context, from, matrix, in_near));    /* 00165A18 */
        memcpy(matrix, a->bytes + 0xD0, sizeof matrix);
        FAULT(w->transform(w->context, to, matrix, in_far));       /* 00165A30 */
        FAULT(w->sweep(w->context, a, from, to, 7, &r));           /* 00165A4C: 0019AFE0 */
        if (!(r & 6)) return 0;                                    /* 00165A54 */
        FAULT(w->ledge_top(w->context, a, 1, &r));                 /* 00165A64: 00178910 */
        if (r == 0) return 0;
        set8(a, 0x1F0, 0x12);                        /* 00165A78 */
        set8(a, 0x1F1, 2);                           /* 00165A80 */
        FAULT(w->clip_E1D0(w->context, a, u8(a, 0x2F1), fl(K_1)));   /* 00165A90 */
        set8(a, 6, 0x21);                            /* 00165AA0 */
        return 0;
    }
    FAULT(hang_clear(c, &r));                        /* 00165AA8 */
    if (r != 0) { hang_drop(a); return 0; }
    FAULT(w->steer_input(w->context, a));            /* 00165AD0 */
    FAULT(steer_left_side(c, &differs));             /* 00165AD8..00165AF0 */
    if (!differs) return 0;
    set8(a, 6, 0x31);                                /* 00165AFC */
    return w->clip_E150(w->context, a, u8(a, 0x2F1), fl(K_1));    /* 00165B0C */
}

static int all_bound(const EmPlayerHangWorkers *w)
{
    return w->scene && w->node && w->aim_track && w->hang_clear && w->steer_input &&
           w->ledge_ahead && w->ledge_above && w->ledge_side && w->request && w->sound &&
           w->skeleton && w->translate && w->floor && w->column && w->land_sound && w->heading &&
           w->reentry && w->handoff && w->fall && w->clip_DF70 && w->clip_DFB0 && w->clip_E0D0 &&
           w->clip_E150 && w->clip_E1D0 && w->clip_FC80 && w->clip_FF80 && w->clip_row &&
           w->sound_100 && w->sound_109 && w->sweep && w->ledge_top && w->transform && w->vadd &&
           w->sqrt && w->cosine && w->sine && w->wrap && w->approach;
}

/* The +6 dispatch. Tail calls return the worker's raw result; the public
 * entry below normalizes it to the state-callback contract (0 or -1). */
static int hang_dispatch(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerHangWorkers *w = context;
    if (!w || !actor || !all_bound(w)) return -1;
    HangCall call;
    memset(&call, 0, sizeof call);
    call.w = w;
    call.a = actor;
    FAULT(w->scene(w->context, &call.scene));
    uint8_t st = u8(actor, 6);                       /* 001647DC: the jump chain on +6 */
    switch (st) {
    case 0: return state_0(&call, st);
    case 1: return hold(&call);                      /* 001648BC */
    case 2:                                          /* 00164BAC */
        if (w32(actor, 0x200) & 0x8000u) return 0;
        set8(actor, 6, (uint8_t)(st + 1));           /* 00164BC0 */
        return sound(&call, 0x12C);                  /* 00164BD0 */
    case 3: return state_3(&call);
    case 4: return state_4(&call);
    case 5:                                          /* 00164EB4 */
        FAULT(w->translate(w->context, actor, 1));
        if (w32(actor, 0x200) & 0x8000u) return 0;   /* 00164EBC..00164EC4 */
        return w->handoff(w->context, actor);        /* 00164ECC: 0017C540 */
    case 0xA:                                        /* 00164EE0 */
        set8(actor, 6, (uint8_t)(st + 1));
        FAULT(request(&call, 0x80, K_4));            /* 00164EF0 */
        set32(actor, 0x21C, 0);                      /* 00164EF8 */
        set32(actor, 0x38, 0);                       /* 00164EFC */
        set32(actor, 0x2E4, 0);                      /* 00164F00 */
        set32(actor, 0x2F4, w32(actor, 0xB4));       /* 00164F04 / 00164F0C */
        return 0;
    case 0xB: return state_B(&call);
    case 0x14: return state_14(&call);
    case 0x1E: case 0x28: return turn_start(&call);
    case 0x1F: case 0x29: return turn_end(&call);
    case 0x20: return state_20(&call);
    case 0x21: return state_21(&call);
    case 0x22: return state_22(&call, st);
    case 0x23: return state_23(&call);
    case 0x27: case 0x31: return turn_back(&call);
    case 0x2A: return state_2A(&call);
    default: return 0;                               /* 001648CC */
    }
}

int em_player_hang_state(void *context, EmPlayerLiveActor *actor)
{
    return hang_dispatch(context, actor) < 0 ? -1 : 0;
}
