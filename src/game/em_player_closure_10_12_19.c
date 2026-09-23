/* em_player_closure_10_12_19.c - the player's +5 0x10, 0x12, 0x19 and 0x1A
 * states (see em_player_closure_10_12_19.h,
 * docs/PLAYER_CLOSURE_10_12_19.md).
 *
 * Sources read:
 *   - byte-matched decomp C (ground truth): 0016DE40, 001696A0, 0016ADE0,
 *     00181B80, 00182090, 00179010, 00175390, 00181950, 001787B0, 001818D0,
 *     00181BA0, 001811F0, 00181A70, 001B0B50;
 *   - the original instructions (build/asm of the decomp) for the NEARMISS
 *     00169730, 0016AE40, 0016EBA0, 0016A4B0, 001814E0, 00181730,
 *     00181E20, 00181F60, 00179910, 0016A8B0 and the asm-word 00181430 /
 *     00182100.
 *     Where the NEARMISS C of 0016EBA0 differs from its instructions, the
 *     instructions are followed: case 0 calls 001B0B50 with no argument
 *     (the C passes the state byte), and case 0x15 hands 00179880 the record
 *     in $a0 and p+2EC in $a1 (the C passes only p+2EC).
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_closure_10_12_19_reference.py executes all of them
 * against this file. */
#include "game/em_player_closure_10_12_19.h"
#include "game/em_player_fall.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define CALL(expression) do { if ((expression) < 0) return -1; } while (0)

typedef EmPlayerClosure1019Workers Workers;
typedef EmPlayerLiveActor Actor;

/* Float constants as the instructions build them (lui/ori). */
#define F_ZERO    UINT32_C(0x00000000)
#define F_ONE     UINT32_C(0x3F800000)
#define F_0_5     UINT32_C(0x3F000000)
#define F_0_7     UINT32_C(0x3F333333)
#define F_0_08    UINT32_C(0x3DA3D70A)
#define F_M0_08   UINT32_C(0xBDA3D70A)
#define F_M0_2    UINT32_C(0xBE4CCCCD)
#define F_0_4     UINT32_C(0x3ECCCCCD)
#define F_1_5     UINT32_C(0x3FC00000)
#define F_2_25    UINT32_C(0x40100000)
#define F_3       UINT32_C(0x40400000)
#define F_4       UINT32_C(0x40800000)
#define F_4_4     UINT32_C(0x408CCCCD)
#define F_4_5     UINT32_C(0x40900000)
#define F_5       UINT32_C(0x40A00000)
#define F_M3      UINT32_C(0xC0400000)
#define F_M5      UINT32_C(0xC0A00000)
#define F_M6      UINT32_C(0xC0C00000)
#define F_5_5     UINT32_C(0x40B00000)
#define F_6       UINT32_C(0x40C00000)
#define F_6_65    UINT32_C(0x40D4CCCD)
#define F_6_75    UINT32_C(0x40D80000)
#define F_8       UINT32_C(0x41000000)
#define F_9       UINT32_C(0x41100000)
#define F_10      UINT32_C(0x41200000)
#define F_11_15   UINT32_C(0x41326666)
#define F_12      UINT32_C(0x41400000)
#define F_14_5    UINT32_C(0x41680000)
#define F_16      UINT32_C(0x41800000)
#define F_18      UINT32_C(0x41900000)
#define F_M18     UINT32_C(0xC1900000)
#define F_20      UINT32_C(0x41A00000)
#define F_20_5    UINT32_C(0x41A40000)
#define F_21      UINT32_C(0x41A80000)
#define F_25      UINT32_C(0x41C80000)
#define F_110     UINT32_C(0x42DC0000)
#define F_120     UINT32_C(0x42F00000)
#define F_256     UINT32_C(0x43800000)
#define F_PI      UINT32_C(0x40490FDB)
#define F_HALF_PI UINT32_C(0x3FC90FDB)
#define F_3HALF_PI UINT32_C(0x4096CBE4)  /* 4.712389 */
#define F_ANGLE   UINT32_C(0x3D8EFA35)   /* 0.06981317 (4 degrees) */
#define F_M4_01   UINT32_C(0xC08051EC)
#define F_M21     UINT32_C(0xC1A80000)
#define F_SWING   UINT32_C(0x3D80ADFD)   /* 0.06283186 (pi / 50) */

/* D_002488B0 (24.0), read by 0016DE40 case 0x15. */
#define D_002488B0 UINT32_C(0x41C00000)
/* D_00248630: the push speed by +25C (0016A4B0 sub-state 3); four entries. */
static const uint32_t kPushSpeed[4] = {
    UINT32_C(0x00000000), UINT32_C(0x3E4CCCCD), UINT32_C(0x3ECCCCCD), UINT32_C(0x3F4CCCCD)
};
/* D_00248640: the push swing rate by +25C (0016A8B0); four entries. */
static const uint32_t kSwingRate[4] = {
    UINT32_C(0x00000000), UINT32_C(0x3D0F5C29), UINT32_C(0x3D23D70A), UINT32_C(0x3D4CCCCD)
};
/* D_00248950: the yaw offsets of 00181950's three probes (0, pi/4, -pi/4). */
static const uint32_t kProbeYaw[3] = {
    UINT32_C(0x00000000), UINT32_C(0x3F490FDB), UINT32_C(0xBF490FDB)
};
/* D_002754B8: the two z offsets of 00181730's probes (-1.0, 1.0). */
static const uint32_t kSideZ[2] = { UINT32_C(0xBF800000), UINT32_C(0x3F800000) };

static uint8_t u8(const Actor *a, unsigned at) { return em_live_u8(a, at); }
static void set8(Actor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static uint32_t u32(const Actor *a, unsigned at) { return em_live_u32(a, at); }
static void set32(Actor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static int16_t s16(const Actor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void set16(Actor *a, unsigned at, unsigned v) { em_live_set_u16(a, at, (uint16_t)v); }
static float fl(uint32_t bits) { return em_ee_float(bits); }
static uint32_t fb(float value) { return em_ee_bits(value); }
static void vec4(uint32_t out[4], uint32_t x, uint32_t y, uint32_t z, uint32_t w)
{
    out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}
static void words(uint32_t *out, const Actor *a, unsigned at, unsigned count)
{
    for (unsigned i = 0; i < count; i++) out[i] = u32(a, at + 4 * i);
}
/* The attribute byte *(*(0x700031D0) + 0x1A) of a probe hit. */
static unsigned attribute(const EmPlayerProbeHit *hit) { return hit->node & 0xFFu; }

typedef struct Run {
    const Workers *w;
    void *c;
    EmPlayerClosure1019Scene *scene;
    EmPlayerClosure1019Scratch *s;
    EmPlayerMajor2Scene *major2;
} Run;

int em_player_closure1019_bound(const EmPlayerClosure1019 *k)
{
    if (!k || !k->workers || !k->scene || !k->scratch || !k->major2) return 0;
    const Workers *w = k->workers;
    return w->request && w->clip_885B0 && w->clip_88610 && w->clip_88550 && w->stick &&
           w->steer && w->floor && w->translate && w->random5 && w->sound && w->sound_1FB9F0 &&
           w->wrap && w->approach && w->cosine && w->sine && w->atan2 && w->sqrt && w->to_int &&
           w->trs && w->apply && w->vadd && w->identity && w->rotate_y && w->translate_m &&
           w->segment && w->move && w->sweep && w->ground && w->slope && w->midpoint &&
           w->ledge_top && w->area_point && w->skeleton && w->script_1B0460 && w->fade &&
           w->fade_1AEE10 && w->use && w->use_probe && w->arbiter && w->rotate_x && w->land &&
           w->surface5d && w->teleport && w->land_sound && w->sound_182A70 && w->clip_FC80 &&
           w->root_node && w->bone && w->floor_query && w->w00179150;
}

static int begin(void *context, Actor *a, Run *r)
{
    const EmPlayerClosure1019 *k = context;
    if (!a || !em_player_closure1019_bound(k)) return -1;
    r->w = k->workers;
    r->c = k->workers->context;
    r->scene = k->scene;
    r->s = k->scratch;
    r->major2 = k->major2;
    return 0;
}

/* ---- shared worker calls ------------------------------------------------ */

static int request(Run *r, Actor *a, int clip, uint32_t blend)
{
    return r->w->request(r->c, a, clip, 0, fl(blend));
}

/* 001749A0(p, clipper(p), 0, blend): the clip the chooser returns. */
static int request_885B0(Run *r, Actor *a, uint32_t blend)
{
    int clip = 0;
    CALL(r->w->clip_885B0(r->c, a, &clip));
    return request(r, a, clip, blend);
}

static int sound(Run *r, Actor *a, int id)
{
    return r->w->sound(r->c, a, id, 0, 300.0f);
}

/* 001FBD50(p, 00179B90() + base, 0, 300.0). */
static int random_sound(Run *r, Actor *a, int base)
{
    int value = 0;
    CALL(r->w->random5(r->c, &value));
    return sound(r, a, value + base);
}

static int wrap(Run *r, uint32_t x, uint32_t *out) { return r->w->wrap(r->c, x, out); }

static int apply(Run *r, const uint32_t matrix[16], const uint32_t v[4], uint32_t out[4])
{
    return r->w->apply(r->c, matrix, v, out);
}

/* 001026A0(out, p+D0, v). */
static int apply_body(Run *r, const Actor *a, const uint32_t v[4], uint32_t out[4])
{
    uint32_t m[16];
    words(m, a, 0xD0, 16);
    return apply(r, m, v, out);
}

/* build_trs_matrix(p+D0, p+B0, p+C0, p+60). */
static int trs(Run *r, Actor *a)
{
    uint32_t out[16], position[3], rotation[3], scale[3];
    words(position, a, 0xB0, 3);
    words(rotation, a, 0xC0, 3);
    words(scale, a, 0x60, 3);
    words(out, a, 0xD0, 16);
    CALL(r->w->trs(r->c, out, position, rotation, scale));
    for (unsigned i = 0; i < 16; i++) set32(a, 0xD0 + 4 * i, out[i]);
    return 0;
}

static int floor1(Run *r, Actor *a, int *result) { return r->w->floor(r->c, a, 1, result); }

/* The root-motion step: +38 = node+8 - +21C; +21C = node+8 (each word is
 * loaded where the original loads it). */
static int root_step(Run *r, Actor *a)
{
    uint32_t node;
    CALL(r->w->root_node(r->c, 8, &node));
    set32(a, 0x38, em_ee_sub_bits(node, u32(a, 0x21C)));
    CALL(r->w->root_node(r->c, 8, &node));
    set32(a, 0x21C, node);
    return 0;
}

/* The use-button test D_00810E74 & 0x70003B76. */
static int use_pressed(const Run *r)
{
    return (r->scene->pad_pressed & r->scene->use_mask) != 0;
}

/* 00102948(dst, src): the quadword copy. */
static void copy4(Actor *a, unsigned dst, unsigned src)
{
    uint32_t v[4];
    words(v, a, src, 4);
    for (unsigned i = 0; i < 4; i++) set32(a, dst + 4 * i, v[i]);
}

/* 001031E0(dst, src): three words. */
static void copy3(uint32_t dst[3], const uint32_t src[3])
{
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

/* ---- 001B0B50 ----------------------------------------------------------- */

void em_player_closure1019_001B0B50(EmPlayerClosure1019Scene *scene)
{
    if (!scene) return;
    if (scene->d8106C8 & 1) scene->d8106BE = 1;                 /* 001B0B64 */
    else if (scene->d8106C8 & 2) scene->d8106BE = 0x81;         /* 001B0B84 */
    else scene->d8106BE = 0;                                    /* 001B0B90 */
}

/* ---- 001696A0 / 0016ADE0: the use-button exit to +5 0x13 ---------------- */

static int use_exit(const Run *r)
{
    return (r->scene->pad_held & r->scene->mask_3B7C) != 0 ||
           (r->scene->pad_held & r->scene->mask_3B7E) != 0;
}

/* 001696A0: +5 = 0x13, +6 = 0, +1F0 = 0x25, D_00275B14 = 0x34, and the
 * saved motion; +B0/+B8 back to +290/+298. */
static int e001696A0(Run *r, Actor *a)
{
    if (!use_exit(r)) return 0;
    set8(a, 5, 0x13);
    set8(a, 6, 0);
    set8(a, 0x1F0, 0x25);
    r->major2->d275B14 = 0x34;
    r->scene->d275B10 = u32(a, 0x2E0);
    r->scene->d275B0C = u32(a, 0x2E8);
    r->scene->d281B64 = u32(a, 0xC4);
    set32(a, 0xB0, u32(a, 0x290));
    set32(a, 0xB8, u32(a, 0x298));
    return 1;
}

/* 0016ADE0: +5 = 0x13, +6 = 0, +1F0 = 0x25, D_00275B14 = 0x1E. */
static int e0016ADE0(Run *r, Actor *a)
{
    if (!use_exit(r)) return 0;
    set8(a, 5, 0x13);
    set8(a, 6, 0);
    set8(a, 0x1F0, 0x25);
    r->major2->d275B14 = 0x1E;
    return 1;
}

/* ---- 00181B80, 00181BA0: the step anchor -------------------------------- */

static void e00181B80(Actor *a)
{
    set32(a, 0x2F4, u32(a, 0xB0));
    set32(a, 0x2F8, u32(a, 0xB8));
}

/* 00181BA0: the step lengths of the +20C clip into 0x70003A20 / 24, then
 * +B0/+B8 = +2F4/+2F8 + the body-frame step, +290/+298 += the second one. */
static int e00181BA0(Run *r, Actor *a)
{
    EmPlayerClosure1019Scratch *s = r->s;
    switch (s16(a, 0x20C)) {
    case 0xBC: case 0xC3: s->s3A20 = F_2_25; s->s3A24 = F_4_5; break;
    case 0xC1: case 0xC8: s->s3A24 = F_ZERO; s->s3A20 = F_2_25; break;
    case 0xC2: case 0xC9: s->s3A24 = F_ZERO; s->s3A20 = F_4_5; break;
    case 0xBE: case 0xC4: s->s3A20 = F_4_5; s->s3A24 = F_4_5; break;
    case 0xC0: case 0xC6: s->s3A20 = F_6_75; s->s3A24 = F_9; break;
    case 0xBD: case 0xC5: s->s3A20 = F_6_75; s->s3A24 = F_4_5; break;
    case 0xBF: case 0xC7: s->s3A20 = F_9; s->s3A24 = F_9; break;
    default: break;
    }
    vec4(s->s38A0, F_ZERO, F_ZERO, s->s3A20, F_ZERO);
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    set32(a, 0xB0, em_ee_add_bits(u32(a, 0x2F4), s->s38B0[0]));
    set32(a, 0xB8, em_ee_add_bits(u32(a, 0x2F8), s->s38B0[2]));
    vec4(s->s38A0, F_ZERO, F_ZERO, s->s3A24, F_ZERO);
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    set32(a, 0x290, em_ee_add_bits(u32(a, 0x290), s->s38B0[0]));
    set32(a, 0x298, em_ee_add_bits(u32(a, 0x298), s->s38B0[2]));
    return 0;
}

/* ---- 001811F0, 00181430: the alternating step clips ---------------------- */

static int e001811F0(Run *r, Actor *a)
{
    int second = u8(a, 0x2F1) == 1;
    int hip = u8(a, 0x25C) == 3;
    unsigned sub = u8(a, 0x23F);
    int clip;
    set8(a, 0x2F1, second ? 2 : 1);
    if (second) clip = hip ? (sub == 1 || sub == 2 ? 0xC5 : 0xC7)
                           : (sub == 1 || sub == 2 ? 0xC4 : 0xC6);
    else clip = hip ? (sub == 1 || sub == 2 ? 0xBD : 0xBF)
                    : (sub == 1 || sub == 2 ? 0xBE : 0xC0);
    CALL(request(r, a, clip, F_ONE));
    set32(a, 0x26C, sub == 1 ? F_0_7 : F_ONE);
    return 0;
}

static int e00181430(Run *r, Actor *a)
{
    int clip;
    if (u8(a, 0x2F1) == 1) clip = u8(a, 0x25C) == 3 ? 0xC9 : 0xC8;
    else clip = u8(a, 0x25C) == 3 ? 0xC2 : 0xC1;
    return request(r, a, clip, F_ONE);
}

/* ---- the probes of 00169730 --------------------------------------------- */

/* One 0019A570 segment from 0x700038B0 up 25 units (0x700038C0), after
 * 001026A0(0x700038B0, p+D0, 0x700038A0) and 001031E0. */
static int body_segment(Run *r, Actor *a, int *result, EmPlayerProbeHit *hit)
{
    EmPlayerClosure1019Scratch *s = r->s;
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    copy3(s->s38C0, s->s38B0);
    s->s38C0[1] = em_ee_add_bits(s->s38C0[1], F_25);
    return r->w->segment(r->c, s->s38B0, s->s38C0, 4, 0, result, hit);
}

/* 001814E0(p, arg): the ladder/rung test ahead; +D = the attribute. */
static int e001814E0(Run *r, Actor *a, int arg, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    *out = 0;
    if (arg == 0) vec4(s->s38A0, F_ZERO, F_ZERO, F_4_4, F_ONE);
    else if (u8(a, 0x23F) == 3) vec4(s->s38A0, F_ZERO, F_ZERO, F_11_15, F_ONE);
    else vec4(s->s38A0, F_ZERO, F_ZERO, F_6_65, F_ONE);
    memset(&hit, 0, sizeof hit);
    CALL(body_segment(r, a, &result, &hit));
    if (result != 0) {
        set8(a, 0xD, attribute(&hit));
        if (u8(a, 0xD) == 0x34 || u8(a, 0xD) == 0x1E) {
            *out = 1;
            return 0;
        }
    }
    /* A hit of any other attribute also takes the +23F == 3 retry
     * (00181634); the NEARMISS C returns 0 there. */
    if (u8(a, 0x23F) != 3) return 0;
    vec4(s->s38A0, F_ZERO, F_ZERO, F_6_65, F_ONE);
    memset(&hit, 0, sizeof hit);
    CALL(body_segment(r, a, &result, &hit));
    if (result != 0) {
        set8(a, 0xD, attribute(&hit));
        if (u8(a, 0xD) == 0x34 || u8(a, 0xD) == 0x1E) *out = 2;
    }
    return 0;
}

/* 00181730(p, side): two probes 3 units to the side (z -1, +1) for a 0x34
 * node over the 10-unit column 20.5 above. */
static int e00181730(Run *r, Actor *a, int side, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    *out = 0;
    for (int i = 0; i < 2; i++) {
        uint32_t x = side == 0 ? em_ee_sub_bits(u32(a, 0x38), F_3)
                               : em_ee_add_bits(F_3, u32(a, 0x38));
        vec4(s->s38A0, x, F_ZERO, kSideZ[i], F_ONE);
        CALL(apply_body(r, a, s->s38A0, s->s38B0));
        uint32_t b0 = s->s38B0[0], b8;
        uint32_t t = em_ee_add_bits(F_20_5, s->s38B0[1]);
        s->s38C0[0] = b0;
        s->s38D0[0] = b0;
        b8 = s->s38B0[2];
        s->s38C0[1] = em_ee_sub_bits(t, F_5);
        s->s38C0[2] = b8;
        s->s38D0[1] = em_ee_add_bits(F_5, t);
        s->s38D0[2] = b8;
        EmPlayerProbeHit hit;
        int result = 0;
        memset(&hit, 0, sizeof hit);
        CALL(r->w->segment(r->c, s->s38C0, s->s38D0, 4, 0, &result, &hit));
        if (result != 0 && attribute(&hit) == 0x34) {
            *out = 1;
            return 0;
        }
    }
    return 0;
}

/* 001818D0: 0019AD00 at the body point 20 ahead. */
static int e001818D0(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    vec4(s->s38A0, F_ZERO, F_ZERO, F_20, F_ONE);
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    memset(&hit, 0, sizeof hit);
    CALL(r->w->move(r->c, a, s->s38B0, 7, &result, &hit));
    *out = result != 0;
    return 0;
}

/* 001787B0(p, side): the 0019AFE0 sweep beside the body for a 0x32 ledge
 * that 001782A0 accepts. */
static int e001787B0(Run *r, Actor *a, int side, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    *out = 0;
    vec4(s->s38A0, side == 0 ? F_M18 : F_18, F_18, F_ZERO, F_ONE);
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    vec4(s->s38A0, F_ZERO, F_ZERO, F_14_5, F_ZERO);
    CALL(apply_body(r, a, s->s38A0, s->s38C0));
    uint32_t sum[4];
    CALL(r->w->vadd(r->c, s->s38C0, s->s38B0, sum));
    memcpy(s->s38C0, sum, sizeof sum);
    s->s38C0[3] = F_ONE;
    memset(&hit, 0, sizeof hit);
    CALL(r->w->sweep(r->c, a, s->s38B0, s->s38C0, 7, &result, &hit));
    if ((result & 6) != 0 && attribute(&hit) == 0x32) {
        int top = 0;
        CALL(r->w->ledge_top(r->c, a, &top));
        if (top != 0) *out = 1;
    }
    return 0;
}

/* ---- the probes of 0016AE40 and 0016A4B0 ------------------------------- */

/* The yaw matrix at 0x700036A0: 001029C0, 00102BB0(yaw), 00102918(p+B0). */
static int yaw_matrix(Run *r, Actor *a, uint32_t yaw)
{
    EmPlayerClosure1019Scratch *s = r->s;
    uint32_t v[4], out[16];
    CALL(r->w->rotate_y(r->c, out, s->s36A0, yaw));
    memcpy(s->s36A0, out, sizeof out);
    words(v, a, 0xB0, 4);
    CALL(r->w->translate_m(r->c, out, s->s36A0, v));
    memcpy(s->s36A0, out, sizeof out);
    return 0;
}

/* 00181E20(p, frame, reach): a 0x1E node on the 25-unit segment up from the
 * point `reach` + 1 ahead (frame 0: the +218 heading, else p+D0). */
static int e00181E20(Run *r, Actor *a, int frame, uint32_t reach, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    *out = 0;
    vec4(s->s38A0, F_ZERO, F_ZERO, em_ee_add_bits(F_ONE, reach), F_ONE);
    if (frame == 0) {
        CALL(r->w->identity(r->c, s->s36A0));
        CALL(yaw_matrix(r, a, u32(a, 0x218)));
        CALL(apply(r, s->s36A0, s->s38A0, s->s38B0));
    } else {
        CALL(apply_body(r, a, s->s38A0, s->s38B0));
    }
    copy3(s->s38C0, s->s38B0);
    s->s38C0[1] = em_ee_add_bits(s->s38C0[1], F_25);
    memset(&hit, 0, sizeof hit);
    CALL(r->w->segment(r->c, s->s38B0, s->s38C0, 4, 0, &result, &hit));
    if (result != 0) *out = attribute(&hit) == 0x1E;
    return 0;
}

/* 00181F60: a 0x36 node on the segment 4 ahead: +290 = the edge midpoint,
 * +218 = wrap(pi/2 + atan2(-axis.z, axis.x)). */
static int e00181F60(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    *out = 0;
    vec4(s->s38A0, F_ZERO, F_ZERO, F_4, F_ONE);
    CALL(apply_body(r, a, s->s38A0, s->s38B0));
    copy3(s->s38C0, s->s38B0);
    s->s38C0[1] = em_ee_add_bits(s->s38C0[1], F_25);
    memset(&hit, 0, sizeof hit);
    CALL(r->w->segment(r->c, s->s38B0, s->s38C0, 4, 0, &result, &hit));
    if (result == 0 || attribute(&hit) != 0x36) return 0;
    uint32_t mid[3];
    int written = 0;
    words(mid, a, 0x290, 3);
    CALL(r->w->midpoint(r->c, mid, &written));
    if (written) for (unsigned i = 0; i < 3; i++) set32(a, 0x290 + 4 * i, mid[i]);
    s->s3A20 = fb(r->w->atan2(r->c, fl(em_ee_neg_bits(fb(hit.axis[2]))), hit.axis[0]));
    CALL(wrap(r, em_ee_add_bits(F_HALF_PI, s->s3A20), &s->s3A20));
    set32(a, 0x218, s->s3A20);
    *out = 1;
    return 0;
}

/* 00182090: +C4 steps toward +218 while +23F != 0; 1 when it arrives. */
static int e00182090(Run *r, Actor *a, int *out)
{
    *out = 0;
    if (u8(a, 0x23F) == 0) return 0;
    uint32_t v;
    CALL(r->w->approach(r->c, u32(a, 0x218), u32(a, 0xC4), F_ANGLE, &v));
    set32(a, 0xC4, v);
    if (em_ee_c_eq_bits(v, u32(a, 0x218))) *out = 1;
    return 0;
}

/* 00182100: the horizontal distance from +B0/+B8 to the node *(p+160)
 * (+2F1 == 1) or *(p+15C) (+2F1 == 2); 0.0 otherwise. */
static int e00182100(Run *r, Actor *a, uint32_t *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    unsigned slot;
    if (u8(a, 0x2F1) == 1) slot = 0x160;
    else if (u8(a, 0x2F1) == 2) slot = 0x15C;
    else { *out = F_ZERO; return 0; }
    uint32_t x, z;
    CALL(r->w->bone(r->c, a, slot, 0xC0, &x));
    s->s3A20 = em_ee_sub_bits(x, u32(a, 0xB0));
    CALL(r->w->bone(r->c, a, slot, 0xC8, &z));
    uint32_t acc = em_ee_mula_bits(s->s3A20, s->s3A20);
    uint32_t dz = em_ee_sub_bits(z, u32(a, 0xB8));
    s->s3A24 = dz;
    *out = fb(r->w->sqrt(r->c, fl(em_ee_madd_bits(acc, dz, dz))));
    return 0;
}

/* 00175390: the pad stick into +23F / +244 / +248 / +24C and the heading
 * +218; *out is +23F. */
static int e00175390(Run *r, Actor *a, int *out)
{
    const EmPlayerClosure1019Scene *sc = r->scene;
    set8(a, 0x23F, sc->pad_gait);
    if (u8(a, 0x23F) == 0) {
        set32(a, 0x24C, 0);
        set32(a, 0x218, u32(a, 0xC4));
        *out = 0;
        return 0;
    }
    uint32_t y = em_ee_div_bits(em_ee_cvt_s_w_bits(sc->pad_y), F_256);
    uint32_t aa = em_ee_mul_bits(F_PI, y);
    uint32_t x = em_ee_div_bits(em_ee_cvt_s_w_bits(sc->pad_x), F_256);
    set32(a, 0x244, fb(r->w->cosine(r->c, fl(em_ee_mul_bits(F_PI, x)))));
    uint32_t c248 = fb(r->w->cosine(r->c, fl(aa)));
    set32(a, 0x248, c248);
    uint32_t angle = fb(r->w->atan2(r->c, fl(em_ee_neg_bits(c248)), fl(u32(a, 0x244))));
    set32(a, 0x24C, angle);
    uint32_t v;
    CALL(wrap(r, em_ee_add_bits(em_ee_add_bits(F_PI, angle), sc->camera_yaw), &v));
    set32(a, 0x218, v);
    *out = u8(a, 0x23F);
    return 0;
}

/* 00181950: the three forward move probes (4.5 ahead at +C4, +C4 + pi/4,
 * +C4 - pi/4); *out is the first probe's hit bit. */
static int e00181950(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    int mask = 0;
    vec4(s->s38A0, F_ZERO, F_ZERO, F_4_5, F_ONE);
    for (int i = 0; i < 3; i++) {
        CALL(r->w->identity(r->c, s->s36A0));
        uint32_t yaw;
        CALL(wrap(r, em_ee_add_bits(u32(a, 0xC4), kProbeYaw[i]), &yaw));
        CALL(yaw_matrix(r, a, yaw));
        CALL(apply(r, s->s36A0, s->s38A0, s->s38B0));
        EmPlayerProbeHit hit;
        int result = 0;
        memset(&hit, 0, sizeof hit);
        CALL(r->w->move(r->c, a, s->s38B0, 0x80000007u, &result, &hit));
        if (result != 0) mask |= 1 << i;
    }
    *out = mask & 1;
    return 0;
}

/* 00181A70: a 0x32 ledge the move probe finds 10 up / 5.5 ahead that
 * 001782A0 accepts: +2E4 -= 6.0, 1. */
static int e00181A70(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    int result = 0;
    *out = 0;
    CALL(r->w->identity(r->c, s->s36A0));
    CALL(yaw_matrix(r, a, u32(a, 0xC4)));
    vec4(s->s38A0, F_ZERO, F_10, F_5_5, F_ONE);
    CALL(apply(r, s->s36A0, s->s38A0, s->s38B0));
    memset(&hit, 0, sizeof hit);
    CALL(r->w->move(r->c, a, s->s38B0, 0x80000007u, &result, &hit));
    if (result == 0 || attribute(&hit) != 0x32) return 0;
    int top = 0;
    CALL(r->w->ledge_top(r->c, a, &top));
    if (top == 0) return 0;
    set32(a, 0x2E4, em_ee_sub_bits(u32(a, 0x2E4), F_6));
    *out = 1;
    return 0;
}

/* ---- 0016A8B0: the push swing ------------------------------------------- */

/* anim_clip_arbiter(p, clip, 1.0, (float)+28). */
static int arbiter(Run *r, Actor *a, int clip)
{
    return r->w->arbiter(r->c, a, clip, 1.0f, fl(em_ee_cvt_s_w_bits((uint32_t)(int32_t)s16(a, 0x28))));
}

/* 0016A8B0: +25C steps toward +23F (+24C 0) or down, while +28 is 0; the
 * swing +C0 += D_00248640[+25C] * cos(+38) and the phase +38 += pi/50,
 * both wrapped; +B0 = (0, -21, 0) through the pitch/yaw frame at +290;
 * then the +2E / +28 swing counter with the clip 0xD8..0xDB. */
static int e0016A8B0(Run *r, Actor *a)
{
    EmPlayerClosure1019Scratch *s = r->s;
    uint32_t v, sum;
    if (s16(a, 0x28) == 0) {
        uint8_t lo = u8(a, 0x25C);
        if (u32(a, 0x24C) == 0) {
            uint8_t hi = u8(a, 0x23F);
            if (lo < hi) set8(a, 0x25C, lo + 1);
            else if (hi < lo) set8(a, 0x25C, lo - 1);
        } else if (lo != 0) {
            set8(a, 0x25C, lo - 1);
        }
    }
    uint32_t c = fb(r->w->cosine(r->c, fl(u32(a, 0x38))));        /* 0016A920 */
    if (u8(a, 0x25C) > 3) return -1;   /* past D_00248640's four entries */
    sum = em_ee_add_bits(u32(a, 0xC0), em_ee_mul_bits(kSwingRate[u8(a, 0x25C)], c));
    set32(a, 0xC0, sum);
    CALL(wrap(r, sum, &v));
    set32(a, 0xC0, v);
    sum = em_ee_add_bits(u32(a, 0x38), F_SWING);
    set32(a, 0x38, sum);
    CALL(wrap(r, sum, &v));
    set32(a, 0x38, v);
    uint32_t out[16], pos[4];
    CALL(r->w->identity(r->c, s->s36A0));
    CALL(r->w->rotate_x(r->c, out, s->s36A0, u32(a, 0xC0)));
    memcpy(s->s36A0, out, sizeof out);
    CALL(r->w->rotate_y(r->c, out, s->s36A0, u32(a, 0xC4)));
    memcpy(s->s36A0, out, sizeof out);
    words(pos, a, 0x290, 4);
    CALL(r->w->translate_m(r->c, out, s->s36A0, pos));
    memcpy(s->s36A0, out, sizeof out);
    vec4(s->s38A0, F_ZERO, F_M21, F_ZERO, F_ONE);
    CALL(apply(r, s->s36A0, s->s38A0, pos));
    for (unsigned i = 0; i < 4; i++) set32(a, 0xB0 + 4 * i, pos[i]);
    int hip = u8(a, 0x25C) == 3;
    switch (em_live_u16(a, 0x2E)) {
    case 0:
    case 2:
        CALL(arbiter(r, a, em_live_u16(a, 0x2E) == 0 ? (hip ? 0xDA : 0xD8) : (hip ? 0xDB : 0xD9)));
        if (s16(a, 0x28) >= 0x18) set16(a, 0x2E, em_live_u16(a, 0x2E) + 1);
        else set16(a, 0x28, (uint16_t)(s16(a, 0x28) + 1));
        return 0;
    case 1:
        CALL(arbiter(r, a, hip ? 0xDA : 0xD8));
        if (s16(a, 0x28) == 0) set16(a, 0x2E, em_live_u16(a, 0x2E) + 1);
        else set16(a, 0x28, (uint16_t)(s16(a, 0x28) - 1));
        return 0;
    case 3:
        CALL(arbiter(r, a, hip ? 0xDB : 0xD9));
        if (s16(a, 0x28) == 0) set16(a, 0x2E, 0);
        else set16(a, 0x28, (uint16_t)(s16(a, 0x28) - 1));
        return 0;
    default:
        return 0;
    }
}

int em_player_closure1019_0016A8B0(void *context, EmPlayerLiveActor *a)
{
    Run run;
    CALL(begin(context, a, &run));
    return e0016A8B0(&run, a);
}

/* ---- 0016A4B0: the push sub-states (+7) --------------------------------- */

static int e0016A4B0(Run *r, Actor *a)
{
    const Workers *w = r->w;
    uint8_t st = u8(a, 7);
    int result;
    switch (st) {
    case 0:
        set8(a, 7, st + 1);                                      /* 0016A520 */
        CALL(request(r, a, 0xD4, F_4));
        copy4(a, 0x290, 0xB0);                                   /* 00102948 */
        set32(a, 0x294, em_ee_add_bits(u32(a, 0x294), F_21));     /* 0016A550 */
        return 0;
    case 1:
        if (!(u32(a, 0x200) & 0x1000)) return 0;
        set8(a, 7, st + 1);                                      /* 0016A574 */
        CALL(request_885B0(r, a, F_8));
        set16(a, 0x2E, 0);
        set32(a, 0x38, 0);
        set16(a, 0x28, 0);
        set8(a, 0x25C, 0);
        return 0;
    case 2:
        CALL(w->stick(r->c, a));                                 /* 0016A5A4 */
        CALL(e0016A8B0(r, a));                                   /* 0016A5AC */
        if (use_pressed(r)) {
            set8(a, 7, u8(a, 7) + 1);
            return 0;
        }
        if (u8(a, 0x25C) != 0) return 0;
        if (u8(a, 5) == 0x10) set8(a, 6, 0);                     /* 0016A600 */
        else if (u32(a, 0x24C) == 1) set8(a, 6, u8(a, 6) + 1);   /* 0016A620 */
        else {
            set8(a, 6, 0x28);
            set8(a, 0x1F0, 0x22);
        }
        CALL(request_885B0(r, a, F_8));
        set32(a, 0xC0, 0);                                       /* 0016A65C */
        copy4(a, 0xB0, 0x290);                                   /* 00102948 */
        set32(a, 0xB4, em_ee_sub_bits(u32(a, 0xB4), F_21));      /* 0016A678 */
        return 0;
    case 3:
        if (em_live_u16(a, 0x2E) == 3 && s16(a, 0x28) >= 0x18) {
            set8(a, 7, st + 1);                                  /* 0016A698 */
            if (u8(a, 0x25C) > 3) return -1;   /* past D_00248630's four entries */
            uint32_t t = kPushSpeed[u8(a, 0x25C)];
            set32(a, 0x38, t);                                   /* 0016A6C0 */
            set32(a, 0x2E0, em_ee_div_bits(em_ee_neg_bits(t), F_120));
            set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(u32(a, 0xC0)), F_110));
            set32(a, 0x2EC, F_0_4);                              /* 0016A710 */
            CALL(request(r, a, 0xD5, F_ONE));
            set32(a, 0x2F4, u32(a, 0xB4));                       /* 0016A718 */
            return 0;
        }
        CALL(w->stick(r->c, a));
        return e0016A8B0(r, a);
    case 4:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 7, st + 1);                                  /* 0016A74C */
            CALL(request(r, a, 0x72, F_8));
        }
        /* fall through */
    case 5:
        set32(a, 0xC0, em_ee_add_bits(u32(a, 0xC0), u32(a, 0x26C)));   /* 0016A77C */
        if (!em_ee_c_eq_bits(u32(a, 0x38), F_ZERO)) {
            uint32_t v = em_ee_add_bits(u32(a, 0x38), u32(a, 0x2E0));
            set32(a, 0x38, v);                                   /* 0016A7A0 */
            if (em_ee_c_le_bits(v, F_ZERO)) {
                set32(a, 0x38, 0);
                set32(a, 0xC0, 0);
            }
            CALL(w->translate(r->c, a, 0));                      /* 0016A7B4 */
        }
        CALL(e00181950(r, a, &result));                          /* 0016A7C4 */
        if (result) set32(a, 0x38, 0);
        CALL(e00181A70(r, a, &result));                          /* 0016A7DC */
        if (result) {
            set32(a, 0xC0, 0);
            set8(a, 6, 0x5B);
            return 0;
        }
        em_player_fall_drop(a);                                  /* 00179880 */
        CALL(floor1(r, a, &result));
        if (result) {
            CALL(w->land(r->c, a));                              /* 0017C580 */
        } else if (em_ee_c_le_bits(u32(a, 0x38), F_ZERO) &&
                   em_ee_c_eq_bits(u32(a, 0xC0), F_ZERO)) {
            set8(a, 5, 7);                                       /* 0016A860 */
            set8(a, 6, 0);
            set8(a, 0x1F0, 0xD);
        }
        if (u8(a, 0x23A) == 0x5D) CALL(w->surface5d(r->c, a, 0));
        return 0;
    case 0x63:
        return w->teleport(r->c, a, 0x78, 0);                    /* 0016A890 */
    default:
        return 0;
    }
}

/* ---- 00169730 (+5 0x10) --------------------------------------------------- */

/* The shared +7 phases 2..4 run (00169730 +6 0x14). */
static int e00169730_climb(Run *r, Actor *a)
{
    const Workers *w = r->w;
    uint8_t ph = u8(a, 7);
    int result;
    switch (ph) {
    case 0:
        set8(a, 7, ph + 1);
        set8(a, 0x2F1, 1);
        CALL(request(r, a, 0xBC, F_8));
        set8(a, 0x25C, 2);
        set32(a, 0x26C, F_ONE);
        set32(a, 0x38, 0);
        set32(a, 0x21C, 0);
        e00181B80(a);
        return 0;
    case 1:
        if (!(u32(a, 0x200) & 0x8000)) set8(a, 7, ph + 1);
        return 0;
    case 2:
        if (u32(a, 0x200) & 0x1000) {
            CALL(random_sound(r, a, 0x124));
            CALL(e00181BA0(r, a));
            e00181B80(a);
            CALL(trs(r, a));
            if (e001696A0(r, a)) return 0;
            CALL(w->stick(r->c, a));
            if (u32(a, 0x24C) == 0) {
                CALL(e001814E0(r, a, 1, &result));
                if (result == 1 || result == 2) {
                    if (result == 2 && u8(a, 0x23F) == 3) set8(a, 0x23F, 2);
                    CALL(e001811F0(r, a));
                    set8(a, 0x25C, u8(a, 0x23F));
                    set32(a, 0x38, 0);
                    set32(a, 0x21C, 0);
                    return 0;
                }
            }
            set8(a, 7, u8(a, 7) + 1);
            return 0;
        }
        set32(a, 0x204, u32(a, 0x26C));
        CALL(root_step(r, a));
        return w->translate(r->c, a, 0);
    case 3:
        set8(a, 7, ph + 1);
        CALL(e00181430(r, a));
        set32(a, 0x38, 0);
        set32(a, 0x21C, 0);
        return 0;
    case 4:
        if (u32(a, 0x200) & 0x1000) {
            CALL(random_sound(r, a, 0x124));
            CALL(e00181BA0(r, a));
            set8(a, 6, 0);
            return 0;
        }
        CALL(root_step(r, a));
        return w->translate(r->c, a, 0);
    default:
        return 0;
    }
}

/* +6 0x28 / 0x32 phase 1: the side step along the ledge (side 0: +24C 2,
 * 0x3C on a miss; side 1: +24C 3, 0x46). */
static int e00169730_shuffle(Run *r, Actor *a, int side)
{
    const Workers *w = r->w;
    uint8_t ph = u8(a, 7);
    int result;
    switch (ph) {
    case 0:
        set8(a, 7, ph + 1);
        return request(r, a, side == 0 ? 0xCA : 0xCB, F_ONE);
    case 1:
        CALL(w->stick(r->c, a));
        if (u32(a, 0x24C) == (uint32_t)(side == 0 ? 2 : 3)) {
            CALL(e00181730(r, a, side, &result));
            if (result) {
                float c = w->cosine(r->c, fl(u32(a, 0xC4)));
                set32(a, 0xB0, em_ee_add_bits(u32(a, 0xB0), em_ee_mul_bits(u32(a, 0x38), fb(c))));
                float s = w->sine(r->c, fl(u32(a, 0xC4)));
                set32(a, 0xB8, em_ee_sub_bits(u32(a, 0xB8), em_ee_mul_bits(u32(a, 0x38), fb(s))));
            } else {
                set8(a, 6, side == 0 ? 0x3C : 0x46);
                set8(a, 7, 0);
            }
            return 0;
        }
        set8(a, 7, u8(a, 7) + 1);
        return request_885B0(r, a, F_8);
    case 2:
        if (!(u32(a, 0x200) & 0x8000)) set8(a, 6, 0);
        return 0;
    default:
        return 0;
    }
}

/* +6 0x3C / 0x46: the hang at the ledge end (side 0 / 1). */
static int e00169730_end(Run *r, Actor *a, int side)
{
    const Workers *w = r->w;
    uint8_t ph = u8(a, 7);
    int result;
    switch (ph) {
    case 0:
        set8(a, 7, ph + 1);
        return request(r, a, side == 0 ? 0xCC : 0xCD, F_4);
    case 1:
        if (u32(a, 0x200) & 0x1000) {
            CALL(request(r, a, side == 0 ? 0xCE : 0xCF, F_ONE));
            set8(a, 7, u8(a, 7) + 1);
        }
        return 0;
    case 2:
        if (use_pressed(r)) {
            CALL(e001787B0(r, a, side, &result));
            if (result) {
                set8(a, 6, 0x5A);
                set8(a, 0x1F0, 0x12);
                CALL(request(r, a, side == 0 ? 0xD0 : 0xD1, F_ONE));
            }
            return 0;
        }
        CALL(w->stick(r->c, a));
        if (u32(a, 0x24C) != (uint32_t)(side == 0 ? 2 : 3)) {
            CALL(request(r, a, side == 0 ? 0xD2 : 0xD3, F_ONE));
            set8(a, 7, u8(a, 7) + 1);
        }
        return 0;
    case 3:
        if (u32(a, 0x200) & 0x1000) set8(a, 6, 0);
        return 0;
    default:
        return 0;
    }
}

int em_player_closure1019_00169730(void *context, EmPlayerLiveActor *a)
{
    Run run, *r = &run;
    CALL(begin(context, a, r));
    const Workers *w = r->w;
    EmPlayerClosure1019Scratch *s = r->s;
    uint8_t st = u8(a, 6);
    int result;
    uint32_t v;
    switch (st) {
    case 0:
        set8(a, 6, st + 1);                                      /* 00169808 */
        set8(a, 7, 0);
        set8(a, 0x1F0, 0x21);
        set8(a, 0x1F1, 0);
        set8(a, 0x25C, 0);
        set8(a, 0x2F1, 0);
        CALL(request_885B0(r, a, F_16));
        {
            uint32_t p[3];
            words(p, a, 0xB0, 3);                                /* 001031E0 */
            for (unsigned i = 0; i < 3; i++) set32(a, 0x290 + 4 * i, p[i]);
        }
        return 0;
    case 1:
        if (em_player_major2_00181D70(a, r->major2)) return 0;   /* 00169854 */
        if (e001696A0(r, a)) return 0;
        if (use_pressed(r)) {
            set8(a, 6, 0xA);
            set8(a, 0x1F0, 0x23);
            return 0;
        }
        CALL(w->stick(r->c, a));                                 /* 001698A4 */
        switch (u32(a, 0x24C)) {
        case 0:
            CALL(e001814E0(r, a, 0, &result));
            if (result) {
                set8(a, 6, 0x14);
                set8(a, 7, 0);
                set8(a, 0x1F1, 1);
                return 0;
            }
            CALL(e001818D0(r, a, &result));
            if (result) return 0;
            set8(a, 6, 0x50);
            set8(a, 7, 0);
            set8(a, 0x1F0, 0x28);
            return 0;
        case 1:
            set8(a, 6, 0x1E);
            set8(a, 0x1F0, 0x24);
            return 0;
        case 2:
            set32(a, 0x38, F_M0_08);
            CALL(e00181730(r, a, 0, &result));
            set8(a, 6, result ? 0x28 : 0x3C);
            set8(a, 7, 0);
            return 0;
        case 3:
            set32(a, 0x38, F_0_08);
            CALL(e00181730(r, a, 1, &result));
            set8(a, 6, result ? 0x32 : 0x46);
            set8(a, 7, 0);
            return 0;
        default:
            return 0;
        }
    case 0xA:
        set8(a, 6, st + 1);                                      /* 001699C8 */
        CALL(request(r, a, 0xD6, F_ONE));
        set32(a, 0x2F4, u32(a, 0xB4));
        set32(a, 0x2EC, 0);
        return 0;
    case 0xB:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 5, 7);                                       /* 001699FC */
            set8(a, 6, 0);
            set8(a, 0x1F0, 0xD);
        }
        em_player_fall_drop(a);                                  /* 00179880 */
        return floor1(r, a, &result);
    case 0x14:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        return e00169730_climb(r, a);
    case 0x1E:
        set8(a, 6, st + 1);                                      /* 00169CDC */
        return request(r, a, 0xD7, F_ONE);
    case 0x1F:
        if (!(u32(a, 0x200) & 0x1000)) return 0;
        CALL(wrap(r, em_ee_add_bits(F_PI, u32(a, 0xC4)), &v));
        set32(a, 0xC4, v);
        CALL(request_885B0(r, a, F_ZERO));
        set8(a, 6, 0);
        return 0;
    case 0x28:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        return e00169730_shuffle(r, a, 0);
    case 0x32:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        return e00169730_shuffle(r, a, 1);
    case 0x3C:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        return e00169730_end(r, a, 0);
    case 0x46:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        return e00169730_end(r, a, 1);
    case 0x50:
        return e0016A4B0(r, a);
    case 0x5A:
        if (u32(a, 0x200) & 0x1000) set8(a, 6, st + 1);
        return 0;
    case 0x5B: {
        int32_t frames = 0;
        s->s3A20 = F_12;                                         /* 0016A2A0 */
        set8(a, 6, u8(a, 6) + 1);
        CALL(request(r, a, 0xE5, s->s3A20));
        CALL(sound(r, a, 0x187));
        set32(a, 0x2F4, u32(a, 0x2E0));
        set32(a, 0x2F8, u32(a, 0x2E8));
        set32(a, 0x258, u32(a, 0x2E4));
        CALL(w->to_int(r->c, s->s3A20, &frames));
        set16(a, 0x28, (uint16_t)frames);
        set32(a, 0x2E0, em_ee_div_bits(em_ee_sub_bits(u32(a, 0x2F4), u32(a, 0xB0)), s->s3A20));
        set32(a, 0x2E8, em_ee_div_bits(em_ee_sub_bits(u32(a, 0x2F8), u32(a, 0xB8)), s->s3A20));
        set32(a, 0x2E4, em_ee_div_bits(em_ee_sub_bits(u32(a, 0x258), u32(a, 0xB4)), s->s3A20));
        CALL(wrap(r, em_ee_sub_bits(u32(a, 0x218), u32(a, 0xC4)), &v));
        s->s3A24 = v;                                            /* 0016A374 */
        if (em_ee_c_lt_bits(v, F_ZERO))
            set32(a, 0x26C, em_ee_div_bits(em_ee_neg_bits(v), F_8));
        else
            set32(a, 0x26C, em_ee_div_bits(v, F_8));
        return 0;
    }
    case 0x5C:
        if (s16(a, 0x28) == 0) {
            set8(a, 6, st + 1);                                  /* 0016A3D4 */
            CALL(w->sound_182A70(r->c, a));
            set32(a, 0xB0, u32(a, 0x2F4));
            set32(a, 0xB8, u32(a, 0x2F8));
            set32(a, 0xB4, u32(a, 0x258));
            set32(a, 0xC4, u32(a, 0x218));
            return 0;
        }
        set32(a, 0xB0, em_ee_add_bits(u32(a, 0xB0), u32(a, 0x2E0)));
        set32(a, 0xB8, em_ee_add_bits(u32(a, 0xB8), u32(a, 0x2E8)));
        set32(a, 0xB4, em_ee_add_bits(u32(a, 0xB4), u32(a, 0x2E4)));
        CALL(w->approach(r->c, u32(a, 0x218), u32(a, 0xC4), u32(a, 0x26C), &v));
        set32(a, 0xC4, v);
        set16(a, 0x28, (uint16_t)(s16(a, 0x28) - 1));
        return 0;
    case 0x5D:
        if (!(u32(a, 0x200) & 0x1000)) return 0;
        CALL(w->sound_182A70(r->c, a));                          /* 0016A454 */
        set8(a, 5, 0xC);
        set8(a, 6, 0);
        set8(a, 0x1F0, 0x17);
        set8(a, 0xD, 0);
        set8(a, 0x2F1, 0);
        return w->clip_FC80(r->c, a, 16.0f);
    default:
        return 0;
    }
}

/* ---- 0016AE40 (+5 0x12) --------------------------------------------------- */

/* The root-motion step with the 00181E20 reach test (+6 0x14 phases 2/4). */
static int reach_step(Run *r, Actor *a)
{
    uint32_t d;
    int result;
    CALL(root_step(r, a));
    CALL(e00182100(r, a, &d));
    CALL(e00181E20(r, a, 1, em_ee_add_bits(u32(a, 0x38), d), &result));
    if (result) CALL(r->w->translate(r->c, a, 0));
    return 0;
}

/* +6 0x1E / 0x32 phase 0: the first step clip 0xBC at `blend`. */
static int step_start(Run *r, Actor *a, uint32_t blend)
{
    set8(a, 7, u8(a, 7) + 1);
    set8(a, 0x2F1, 1);
    CALL(request(r, a, 0xBC, blend));
    set32(a, 0x26C, F_ONE);
    set8(a, 0x25C, 2);
    set32(a, 0x38, 0);
    set32(a, 0x21C, 0);
    return 0;
}

/* +6 0x1E / 0x32 phase 2. */
static int step_second(Run *r, Actor *a)
{
    if (u32(a, 0x200) & 0x1000) {
        CALL(random_sound(r, a, 0x112));
        set8(a, 7, u8(a, 7) + 1);
        CALL(e00181430(r, a));
        set32(a, 0x38, 0);
        set32(a, 0x21C, 0);
        return 0;
    }
    set32(a, 0x204, u32(a, 0x26C));
    CALL(root_step(r, a));
    return r->w->translate(r->c, a, 0);
}

int em_player_closure1019_0016AE40(void *context, EmPlayerLiveActor *a)
{
    Run run, *r = &run;
    CALL(begin(context, a, r));
    const Workers *w = r->w;
    EmPlayerClosure1019Scratch *s = r->s;
    uint8_t st = u8(a, 6);
    int result;
    uint32_t v;
    switch (st) {
    case 0:
        set8(a, 6, st + 1);
        set8(a, 7, 0);
        set8(a, 0x1F0, 0x22);
        set8(a, 0x1F1, 0);
        set8(a, 0x25C, 0);
        set8(a, 0x2F1, 0);
        CALL(request_885B0(r, a, F_16));
        copy4(a, 0x290, 0xB0);                                   /* 00102948 */
        return 0;
    case 1:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        if (e0016ADE0(r, a)) return 0;
        if (use_pressed(r)) {
            set8(a, 6, 0xA);
            set8(a, 0x1F0, 0x23);
            return 0;
        }
        CALL(e00175390(r, a, &result));                          /* 0016AF78 */
        if (result == 0) return 0;
        CALL(e00181E20(r, a, 0, F_4_5, &result));
        if (result) {
            set8(a, 6, 0x14);
            set8(a, 7, 0);
            set8(a, 0x1F1, 1);
            return 0;
        }
        CALL(e00181F60(r, a, &result));
        if (result) {
            set8(a, 6, 0x1E);
            set8(a, 7, 0);
        }
        return 0;
    case 0xA:
        set8(a, 6, st + 1);
        CALL(request(r, a, 0xD6, F_ONE));
        set32(a, 0x2F4, u32(a, 0xB4));
        set32(a, 0x2EC, 0);
        return 0;
    case 0xB:
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 5, 7);
            set8(a, 6, 0);
            set8(a, 0x1F0, 0xD);
        }
        em_player_fall_drop(a);                                  /* 00179880 */
        return floor1(r, a, &result);
    case 0x14: {
        if (em_player_major2_00181D70(a, r->major2)) return 0;   /* 0016B050 */
        CALL(e00175390(r, a, &result));                          /* 0016B060 */
        uint8_t ph = u8(a, 7);
        switch (ph) {
        case 0:
            set8(a, 7, ph + 1);
            CALL(wrap(r, em_ee_sub_bits(r->scene->spad31E4, u32(a, 0xC4)), &s->s3A20));
            if (!em_ee_c_lt_bits(s->s3A20, F_ZERO)) {
                set8(a, 0x2F1, 1);
                CALL(request(r, a, 0xBC, F_8));
            } else {
                set8(a, 0x2F1, 2);
                CALL(request(r, a, 0xC3, F_8));
            }
            set32(a, 0x26C, F_ONE);
            set8(a, 0x25C, 2);
            set32(a, 0x38, 0);
            set32(a, 0x21C, 0);
            break;
        case 1:
            if (!(u32(a, 0x200) & 0x8000)) set8(a, 7, ph + 1);
            break;
        case 2:
            CALL(e00182090(r, a, &result));
            CALL(trs(r, a));
            if (u32(a, 0x200) & 0x1000) {
                CALL(random_sound(r, a, 0x112));
                if (e0016ADE0(r, a)) return 0;
                if (u8(a, 0x23F) != 0) {
                    uint32_t d;
                    CALL(e00182100(r, a, &d));
                    CALL(e00181E20(r, a, 1, em_ee_add_bits(F_2_25, d), &result));
                    if (result) {
                        CALL(e001811F0(r, a));
                        set8(a, 0x25C, u8(a, 0x23F));
                        set32(a, 0x38, 0);
                        set32(a, 0x21C, 0);
                    } else {
                        set8(a, 7, u8(a, 7) + 1);
                    }
                } else {
                    set8(a, 7, u8(a, 7) + 1);
                }
            } else {
                set32(a, 0x204, u32(a, 0x26C));
                CALL(reach_step(r, a));
            }
            break;
        case 3:
            set8(a, 7, ph + 1);
            CALL(e00181430(r, a));
            set32(a, 0x38, 0);
            set32(a, 0x21C, 0);
            break;
        case 4:
            if (u32(a, 0x200) & 0x1000) {
                CALL(random_sound(r, a, 0x112));
                set8(a, 6, 0);
            } else {
                CALL(reach_step(r, a));
            }
            break;
        default:
            break;
        }
        CALL(e00181950(r, a, &result));
        copy4(a, 0x290, 0xB0);                                   /* 00102948 */
        return 0;
    }
    case 0x1E: {
        CALL(w->approach(r->c, u32(a, 0x218), u32(a, 0xC4), F_ANGLE, &v));
        set32(a, 0xC4, v);
        uint8_t ph = u8(a, 7);
        switch (ph) {
        case 0:
            return step_start(r, a, F_8);
        case 1:
            if (!(u32(a, 0x200) & 0x8000)) set8(a, 7, ph + 1);
            return 0;
        case 2:
            return step_second(r, a);
        case 3:
            if (u32(a, 0x200) & 0x1000) {
                CALL(random_sound(r, a, 0x112));
                set8(a, 6, 0x28);
                set8(a, 7, 0);
                set32(a, 0xB0, u32(a, 0x290));
                set32(a, 0xB8, u32(a, 0x298));
                return 0;
            }
            CALL(root_step(r, a));
            return w->translate(r->c, a, 0);
        default:
            return 0;
        }
    }
    case 0x28:
        if (em_player_major2_00181D70(a, r->major2)) return 0;
        CALL(w->stick(r->c, a));
        if (u32(a, 0x24C) == 0) {
            set8(a, 6, u8(a, 6) + 1);
            set8(a, 7, 0);
            set8(a, 0x1F0, 0x28);
        } else if (u32(a, 0x24C) == 1) {
            set8(a, 6, u8(a, 6) + 2);
            set8(a, 7, 0);
        }
        return 0;
    case 0x29:
        return e0016A4B0(r, a);
    case 0x2A:
        set8(a, 6, st + 1);
        return request(r, a, 0xD7, F_ONE);
    case 0x2B:
        if (!(u32(a, 0x200) & 0x1000)) return 0;
        CALL(wrap(r, em_ee_add_bits(F_PI, u32(a, 0xC4)), &v));
        set32(a, 0xC4, v);
        CALL(request_885B0(r, a, F_ZERO));
        set8(a, 6, 0x32);
        set8(a, 7, 0);
        return 0;
    case 0x32: {
        uint8_t ph = u8(a, 7);
        switch (ph) {
        case 0:
            return step_start(r, a, F_ZERO);
        case 1:
            if (!(u32(a, 0x200) & 0x8000)) set8(a, 7, ph + 1);
            return 0;
        case 2:
            return step_second(r, a);
        case 3:
            if (u32(a, 0x200) & 0x1000) {
                CALL(random_sound(r, a, 0x112));
                set8(a, 6, 0);
                set8(a, 0x1F0, 0x22);
            }
            return 0;
        default:
            return 0;
        }
    }
    default:
        return 0;
    }
}

/* ---- 0016DE40 (+5 0x19) --------------------------------------------------- */

/* 00179010: the ground probe 6 below +B0: +A = 1, +23B = the attribute and
 * +9C = the slope on a hit; +23B = 0 otherwise. *out is +A. */
static int e00179010(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scratch *s = r->s;
    EmPlayerProbeHit hit;
    uint32_t at[4];
    int result = 0;
    vec4(s->s38A0, F_ZERO, F_M6, F_ZERO, F_ONE);
    words(at, a, 0xB0, 4);
    memset(&hit, 0, sizeof hit);
    CALL(r->w->ground(r->c, a, at, s->s38A0, 0x80000006u, &result, &hit));
    if (result != 0) {
        set8(a, 0xA, 1);
        set8(a, 0x23B, attribute(&hit));
        uint32_t slope = u32(a, 0x9C);
        CALL(r->w->slope(r->c, &hit, &slope));
        set32(a, 0x9C, slope);
    } else {
        set8(a, 0x23B, 0);
    }
    *out = u8(a, 0xA);
    return 0;
}

/* The drop to the ground: +B4 -= 1.0 until 00179010 finds it. */
static int settle(Run *r, Actor *a)
{
    int found;
    for (;;) {
        set32(a, 0xB4, em_ee_sub_bits(u32(a, 0xB4), F_ONE));
        CALL(e00179010(r, a, &found));
        if (found) return 0;
    }
}

/* lo <= x <= hi as the original tests it: !(x < lo) && x <= hi. */
static int within(uint32_t x, float lo, float hi)
{
    return !em_ee_c_lt_bits(x, fb(lo)) && em_ee_c_le_bits(x, fb(hi));
}

/* 00179910: the area-2 exit boxes (D_0024D650[2][sub] +120 for sub 0/2,
 * +F0 for sub 1): D_008106B5..B8 and 1 when +B0..+B8 is within 8 of it. */
static int e00179910(Run *r, Actor *a, int *out)
{
    EmPlayerClosure1019Scene *sc = r->scene;
    EmPlayerClosure1019Scratch *s = r->s;
    uint32_t p[3];
    unsigned offset;
    *out = 0;
    if (sc->area != 2) return 0;
    if (sc->sub_area == 0 || sc->sub_area == 2) offset = 0x120;
    else if (sc->sub_area == 1) offset = 0xF0;
    else return 0;
    CALL(r->w->area_point(r->c, sc->area, sc->sub_area, offset, p));
    s->s3A20 = em_ee_sub_bits(p[0], u32(a, 0xB0)) & UINT32_C(0x7FFFFFFF);   /* 0011DF78 */
    s->s3A24 = em_ee_sub_bits(p[1], u32(a, 0xB4)) & UINT32_C(0x7FFFFFFF);
    s->s3A28 = em_ee_sub_bits(p[2], u32(a, 0xB8)) & UINT32_C(0x7FFFFFFF);
    if (!em_ee_c_lt_bits(s->s3A20, F_8) || !em_ee_c_lt_bits(s->s3A24, F_8) ||
        !em_ee_c_lt_bits(s->s3A28, F_8))
        return 0;
    sc->d8106B8 = 1;
    sc->d8106B5 = sc->area;
    if (offset == 0x120) {
        sc->d8106B7 = 6;
        sc->d8106B6 = 1;
    } else {
        sc->d8106B7 = 5;
        sc->d8106B6 = (sc->area_flags2 & 0x80) ? 2 : 0;
    }
    *out = 1;
    return 0;
}

/* The 001823E0 / use-button guard of +6 0xA, 0x14 and 0x15: 1 = stop. */
static int guard_use(Run *r, Actor *a, int *stop)
{
    *stop = 1;
    if (em_player_major2_001823E0(a)) return 0;
    if (use_pressed(r)) {
        int used = 0;
        CALL(r->w->use(r->c, a, &used));
        if (used) return 0;
    }
    *stop = 0;
    return 0;
}

/* 0016DE40 +6 0x15: the crawl. */
static int e0016DE40_crawl(Run *r, Actor *a)
{
    const Workers *w = r->w;
    EmPlayerClosure1019Scene *sc = r->scene;
    int result;
    uint32_t node;
    set32(a, 0x204, u32(a, 0x2F4));                              /* 0016E21C */
    CALL(w->root_node(r->c, 0, &node));
    set32(a, 0x38, em_ee_sub_bits(node, u32(a, 0x21C)));
    CALL(w->root_node(r->c, 0, &node));
    set32(a, 0x21C, node);
    CALL(w->w00179150(r->c, a));
    set32(a, 0xB4, em_ee_add_bits(u32(a, 0xB4), F_M0_2));
    CALL(e00179010(r, a, &result));
    set8(a, 0x302, 0);
    if (u8(a, 0xA) == 0) {
        if (sc->area == 3) {
            uint32_t x = u32(a, 0xB0), y = u32(a, 0xB4), z = u32(a, 0xB8);
            if (within(x, 648.0f, 668.0f) && within(y, 45.0f, 65.0f) && within(z, 704.0f, 724.0f)) {
                sc->zone = 7;
                set8(a, 0x302, 1);
            } else if (within(x, 624.0f, 644.0f) && within(y, 45.0f, 65.0f) &&
                       within(z, 792.0f, 812.0f)) {
                sc->zone = 4;
                set8(a, 0x302, 1);
            } else if (within(x, 547.0f, 567.0f) && within(y, 45.0f, 65.0f) &&
                       within(z, 705.0f, 725.0f)) {
                sc->zone = 2;
                set8(a, 0x302, 1);
            } else {
                sc->zone = 0;
                set8(a, 0x302, 1);
            }
        } else if (sc->area == 8 && sc->sub_area == 3) {
            uint32_t x = u32(a, 0xB0);
            if (within(x, 110.5f, 135.0f) && within(u32(a, 0xB8), 146.0f, 176.0f)) {
                set32(a, 0xB0, fb(123.5f));
                set32(a, 0xB8, fb(156.4f));
                sc->zone = 0;
            } else if (within(x, 122.6f, 142.6f) && within(u32(a, 0xB8), 87.0f, 107.0f)) {
                sc->zone = 2;
                set8(a, 0x302, 1);
            }
        } else if (sc->area == 0x13 && sc->sub_area == 0) {
            if (within(u32(a, 0xB0), 710.0f, 730.0f) && within(u32(a, 0xB8), 960.0f, 1160.0f)) {
                sc->zone = 8;
                set8(a, 0x302, 1);
            }
        }
        uint32_t point[3];
        words(point, a, 0xB0, 3);
        CALL(w->floor_query(r->c, a, point, &result));          /* 00179450 */
        if (result != 0) {
            uint32_t d = u32(a, 0x258);
            if (em_ee_c_lt_bits(d, em_ee_neg_bits(D_002488B0))) {
                set8(a, 0xD, 2);
            } else if (!em_ee_c_le_bits(d, F_M4_01)) {
                set8(a, 0xD, 0);
                CALL(settle(r, a));
            } else {
                set8(a, 0xD, 1);
            }
            set8(a, 6, u8(a, 6) + 1);
        }
    } else if (u8(a, 0x23B) == 0x37) {
        set8(a, 0xD, 0);
        set8(a, 6, u8(a, 6) + 1);
        if (sc->area == 0x13) {
            if (within(u32(a, 0xB4), 230.0f, 250.0f) && within(u32(a, 0xB0), 693.0f, 713.0f) &&
                within(u32(a, 0xB8), 1209.0f, 1229.0f)) {
                sc->zone = 9;
                set8(a, 0x302, 1);
            }
        } else if (sc->area == 3) {
            sc->zone = 0;
            set8(a, 0x302, 1);
        } else if (sc->area == 0) {
            uint32_t y = u32(a, 0xB4);
            if (em_ee_c_le_bits(y, fb(-85.0f))) {
                sc->zone = 8;
                set8(a, 0x302, 1);
            } else if (em_ee_c_le_bits(y, fb(-65.0f))) {
                sc->zone = 9;
                set8(a, 0x302, 1);
            }
        }
    }
    CALL(e00179910(r, a, &result));
    if (result) {
        set8(a, 6, 0x63);
        return w->fade(r->c, 4, 0);
    }
    return 0;
}

/* 0016DE40 +6 0x1F / 0x29: +C4 steps toward +26C; 0x32 on arrival. */
static int turn_to(Run *r, Actor *a)
{
    uint32_t v;
    CALL(r->w->approach(r->c, u32(a, 0x26C), u32(a, 0xC4), F_ANGLE, &v));
    set32(a, 0xC4, v);
    if (em_ee_c_eq_bits(u32(a, 0x26C), u32(a, 0xC4))) set8(a, 6, 0x32);
    return 0;
}

int em_player_closure1019_0016DE40(void *context, EmPlayerLiveActor *a)
{
    Run run, *r = &run;
    CALL(begin(context, a, r));
    const Workers *w = r->w;
    EmPlayerClosure1019Scene *sc = r->scene;
    int result, stop, clip;
    uint32_t v;
    set8(a, 1, 0);
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0:
        sc->d8106BE = 2;
        set8(a, 0x302, 0);
        if (sc->area == 0 && !em_ee_c_le_bits(u32(a, 0xB8), fb(-1470.0f))) {
            set8(a, 0xD, 2);
            set8(a, 5, 0x1A);
            set8(a, 6, 0);
            set8(a, 0x1F0, 0x2E);
            set32(a, 0xB0, fb(185.8f));
            set32(a, 0xB8, fb(-1450.0f));
            set32(a, 0xB4, em_ee_add_bits(u32(a, 0xB4), F_M0_2));
            sc->zone = 5;
            return w->script_1B0460(r->c, 1);
        }
        set8(a, 6, u8(a, 6) + 1);
        set8(a, 7, 0);
        set32(a, 0x38, 0);
        set8(a, 0x1F1, 0);
        clip = 0;
        CALL(w->clip_88610(r->c, a, &clip));
        CALL(request(r, a, clip, F_ZERO));
        return settle(r, a);
    case 1:
        if (sc->fade == 0) set8(a, 6, 0xA);
        return 0;
    case 0xA:
        CALL(guard_use(r, a, &stop));
        if (stop) return 0;
        clip = 0;
        CALL(w->clip_88610(r->c, a, &clip));
        CALL(request(r, a, clip, F_ONE));
        CALL(w->steer(r->c, a));
        switch (u32(a, 0x24C)) {
        case 0: set8(a, 6, 0x14); return 0;
        case 1: set8(a, 6, 0x1E); return 0;
        case 2: case 3:
            CALL(w->use_probe(r->c, a, &result));
            if (result == 0x1F) {
                uint32_t mid[3];
                int written = 0;
                words(mid, a, 0x290, 3);
                CALL(w->midpoint(r->c, mid, &written));
                if (written) for (unsigned i = 0; i < 3; i++) set32(a, 0x290 + 4 * i, mid[i]);
                set32(a, 0xB0, u32(a, 0x290));
                set32(a, 0xB8, u32(a, 0x298));
            }
            set8(a, 6, 0x28);
            return 0;
        default:
            return 0;
        }
    case 0x14: {
        CALL(guard_use(r, a, &stop));
        if (stop) return 0;
        set8(a, 6, u8(a, 6) + 1);
        uint8_t sub = u8(a, 0x23F);
        set32(a, 0x2F4, sub == 1 ? F_0_5 : sub == 2 ? F_0_7 : F_ONE);
        CALL(request(r, a, 0x14B, F_ONE));
        int value = 0;
        CALL(w->random5(r->c, &value));
        CALL(w->sound_1FB9F0(r->c, value + 0x13F, 0x1000, 0x1000, 0x1000));
        set32(a, 0x38, 0);
        set32(a, 0x21C, 0);
        return 0;
    }
    case 0x15:
        CALL(guard_use(r, a, &stop));
        if (stop) return 0;
        if (u32(a, 0x200) & 0x1000) {
            set8(a, 6, 0xA);
            return 0;
        }
        return e0016DE40_crawl(r, a);
    case 0x16:
        set8(a, 6, st + 1);
        return w->fade(r->c, 4, 0);
    case 0x17:
        if (sc->fade != 2) return 0;
        set8(a, 5, 0x1A);
        set8(a, 6, 0);
        set8(a, 0x1F0, 0x2E);
        CALL(w->fade_1AEE10(r->c, 4, 0));
        if (u8(a, 0x302) != 0) return w->script_1B0460(r->c, 1);
        return 0;
    case 0x1E:
        if (em_player_major2_001823E0(a)) return 0;
        set8(a, 6, u8(a, 6) + 1);
        CALL(wrap(r, em_ee_add_bits(F_PI, u32(a, 0xC4)), &v));
        set32(a, 0x26C, v);
        return 0;
    case 0x1F:
        if (em_player_major2_001823E0(a)) return 0;
        return turn_to(r, a);
    case 0x28:
        if (em_player_major2_001823E0(a)) return 0;
        set8(a, 6, u8(a, 6) + 1);
        if (u32(a, 0x24C) == 2)
            CALL(wrap(r, em_ee_sub_bits(u32(a, 0xC4), F_HALF_PI), &v));
        else
            CALL(wrap(r, em_ee_add_bits(F_HALF_PI, u32(a, 0xC4)), &v));
        set32(a, 0x26C, v);
        /* fall through */
    case 0x29:
        if (em_player_major2_001823E0(a)) return 0;
        return turn_to(r, a);
    case 0x32:
        if (em_player_major2_001823E0(a)) return 0;
        set8(a, 6, u8(a, 6) + 1);
        set16(a, 0x28, 0x10);
        /* fall through */
    case 0x33: {
        if (em_player_major2_001823E0(a)) return 0;
        int16_t t = s16(a, 0x28);
        set16(a, 0x28, (uint16_t)(t - 1));
        if (t == 0) set8(a, 6, 0xA);
        return 0;
    }
    default:
        return 0;
    }
}

/* ---- 0016EBA0 (+5 0x1A) --------------------------------------------------- */

int em_player_closure1019_0016EBA0(void *context, EmPlayerLiveActor *a)
{
    Run run, *r = &run;
    CALL(begin(context, a, r));
    const Workers *w = r->w;
    EmPlayerClosure1019Scratch *s = r->s;
    int result, clip;
    uint32_t out[4];
    uint8_t st = u8(a, 6);
    switch (st) {
    case 0:
        em_player_closure1019_001B0B50(r->scene);                    /* 0016EBFC */
        set32(a, 0x38, 0);
        if (u8(a, 0xD) == 0) {
            set8(a, 6, 0xA);
            CALL(request(r, a, 0x153, F_ONE));
            CALL(w->skeleton(r->c, a));
            vec4(s->s38A0, F_ZERO, F_ZERO, F_5, F_ONE);
            CALL(apply_body(r, a, s->s38A0, out));               /* out = p+B0 */
            for (unsigned i = 0; i < 4; i++) set32(a, 0xB0 + 4 * i, out[i]);
            return 0;
        }
        if (u8(a, 0xD) == 1) {
            vec4(s->s38A0, F_ZERO, F_ZERO, F_5, F_ONE);
            CALL(apply_body(r, a, s->s38A0, out));
            for (unsigned i = 0; i < 4; i++) set32(a, 0xB0 + 4 * i, out[i]);
            CALL(trs(r, a));
            CALL(request(r, a, 0x72, F_ONE));
            set8(a, 6, 0x14);
            set32(a, 0x2EC, 0);
            set16(a, 0x28, 8);
            return 0;
        }
        vec4(s->s38A0, F_ZERO, F_M3, F_M5, F_ONE);
        CALL(apply_body(r, a, s->s38A0, s->s38B0));
        {
            EmPlayerProbeHit hit;
            memset(&hit, 0, sizeof hit);
            result = 0;
            CALL(w->move(r->c, a, s->s38B0, 7, &result, &hit));
            if (result == 0) return 0;
            set32(a, 0xB0, em_ee_add_bits(fb(hit.point[0]), em_ee_mul_bits(F_1_5, fb(hit.normal[0]))));
            set32(a, 0xB8, em_ee_add_bits(fb(hit.point[2]), em_ee_mul_bits(F_1_5, fb(hit.normal[2]))));
            set32(a, 0xB4, em_ee_sub_bits(u32(a, 0xB4), F_20_5));
            s->s3A20 = fb(w->atan2(r->c, fl(em_ee_neg_bits(fb(hit.normal[2]))), hit.normal[0]));
            uint32_t v;
            CALL(wrap(r, em_ee_add_bits(F_3HALF_PI, s->s3A20), &v));
            set32(a, 0xC4, v);
            set8(a, 6, 0x1E);
            clip = 0;
            CALL(w->clip_88550(r->c, a, &clip));
            return request(r, a, clip, F_ZERO);
        }
    case 0xA:
    case 0x16:
        if (r->scene->fade == 0 && (u32(a, 0x200) & 0x1000)) {
            set8(a, 5, 0);
            set8(a, 6, 0);
            set8(a, 0x1F0, 0);
        }
        return 0;
    case 0x14: {
        int16_t t = s16(a, 0x28);
        set16(a, 0x28, (uint16_t)(t - 1));
        if (t == 0) set8(a, 6, u8(a, 6) + 1);
        return 0;
    }
    case 0x15:
        em_player_fall_drop(a);                                  /* 00179880 */
        CALL(floor1(r, a, &result));
        if (result == 0) return 0;
        set8(a, 6, u8(a, 6) + 1);
        CALL(w->land_sound(r->c, a, 1));
        return request(r, a, 0x6D, F_8);
    case 0x1E:
        if (r->scene->fade == 0) {
            set8(a, 5, 0x18);
            set8(a, 6, 0);
            set8(a, 0x1F0, 0x2C);
            set8(a, 0x1F1, 0);
            set8(a, 0xD, 2);
        }
        return 0;
    default:
        return 0;
    }
}
