/* em_player_reaction.c - the player's +4 = 2 reaction states (see
 * em_player_reaction.h, docs/PLAYER_REACTION.md).
 *
 * Read from the decomp C where it is byte-matched and from the original
 * instructions where it is not: 0021D800 and 0021D530 are NEARMISS, and
 * 0021E490 and 0021D1A0 are instruction-word files.
 * Where the NEARMISS C of 0021D800 returns early from its sub-state 1, the
 * instructions branch to the +23A 0x5D tail (0021DA8C, 0021DAD8, 0021DB00,
 * 0021DB74 all go to 0021DB84); this translation follows the instructions.
 * tools/test_player_reaction_reference.py executes the original instructions
 * and compares every actor byte, scene byte and worker call. */
#include "game/em_player_reaction.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Binary32 constants as the original loads them (lui/ori or .rodata). */
enum {
    K_0 = 0x00000000u, K_ONE = 0x3F800000u, K_300 = 0x43960000u,
    K_M0_2 = 0xBE4CCCCDu, K_M4 = 0xC0800000u,
    K_PI = 0x40490FDBu, K_MPI = 0xC0490FDBu, K_2PI = 0x40C90FDBu, K_HALF_PI = 0x3FC90FDBu,
    K_0_1 = 0x3DCCCCCDu, K_0_25 = 0x3E800000u, K_0_4 = 0x3ECCCCCDu, K_0_45 = 0x3EE66666u,
    K_0_5 = 0x3F000000u, K_0_75 = 0x3F400000u, K_1_2 = 0x3F99999Au, K_2_2 = 0x400CCCCDu,
    K_10 = 0x41200000u, K_16 = 0x41800000u, K_18 = 0x41900000u, K_22 = 0x41B00000u,
    K_24 = 0x41C00000u, K_30 = 0x41F00000u, K_35 = 0x420C0000u, K_50 = 0x42480000u,
    K_60 = 0x42700000u, K_80 = 0x42A00000u, K_100 = 0x42C80000u,
};

static uint32_t fw(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void set_fw(EmPlayerLiveActor *a, unsigned at, uint32_t bits) { em_live_set_u32(a, at, bits); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void set_b8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }
static int16_t h16(const EmPlayerLiveActor *a, unsigned at) { return (int16_t)em_live_u16(a, at); }
static void set_h16(EmPlayerLiveActor *a, unsigned at, int v) { em_live_set_u16(a, at, (uint16_t)v); }
static uint32_t sbits(float v) { return em_ee_bits(v); }
static float sfloat(uint32_t v) { return em_ee_float(v); }

/* c.eq.s against 0 (the "float truthiness" tests). */
static int is_zero(uint32_t v) { return em_ee_c_eq_bits(v, K_0); }

/* ---- worker calls (NULL or < 0 is a fault) ----------------------------- */

static int request(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a, int clip, int force,
                   uint32_t blend)
{
    return w->request ? w->request(w->context, a, clip, force, sfloat(blend)) : -1;
}
static int sound(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a, unsigned id)
{
    return w->sound ? w->sound(w->context, a, id) : -1;
}
static int rumble(const EmPlayerReactionWorkers *w, int x, int y, int z, int u)
{
    return w->rumble ? w->rumble(w->context, x, y, z, u) : -1;
}
static int floor_service(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a, int *result)
{
    return w->floor ? w->floor(w->context, a, 1, result) : -1;
}
static int translate(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a, int arg)
{
    return w->translate ? w->translate(w->context, a, arg) : -1;
}
static int probes(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a)
{
    return w->probes ? w->probes(w->context, a) : -1;
}

/* ---- helpers ------------------------------------------------------------ */

/* 001B1470: subtract 2pi while above pi, add 2pi while at or below -pi. */
static uint32_t wrap_bits(uint32_t x)
{
    if (!em_ee_c_le_bits(x, K_PI)) {
        do x = em_ee_sub_bits(x, K_2PI); while (!em_ee_c_le_bits(x, K_PI));
    }
    if (em_ee_c_le_bits(x, K_MPI)) {
        do x = em_ee_add_bits(x, K_2PI); while (em_ee_c_le_bits(x, K_MPI));
    }
    return x;
}

float em_player_reaction_wrap(float angle) { return sfloat(wrap_bits(sbits(angle))); }

/* 0017C540. */
void em_player_reaction_0017C540(EmPlayerLiveActor *a)
{
    if (b8(a, 0x25C)) {
        set_b8(a, 5, 1);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 1);
        set_b8(a, 0x1F1, 0);
    } else {
        set_b8(a, 5, 0);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0);
        set_fw(a, 0x38, 0);
    }
    set_b8(a, 4, 1);
}

/* 0021D530 (instructions 0021D530..0021D5F0). */
void em_player_reaction_0021D530(EmPlayerLiveActor *a, const EmPlayerReactionScene *s)
{
    set_b8(a, 4, 1);                                          /* 0021D540 */
    if (b8(a, 0x237)) {
        set_b8(a, 5, 0x1C);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x30);
        return;
    }
    unsigned held = s->pad_held;                               /* D_00810E70 */
    if (held & s->spad3B7E) {
        set_b8(a, 5, 0x1E);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x32);
        set_b8(a, 0x1F1, 0);
        set_b8(a, 0x317, 1);
        return;
    }
    if (held & s->spad3B7C) {
        set_b8(a, 5, 0x1D);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x31);
        set_b8(a, 0x1F1, 0);
        set_b8(a, 0x317, 1);
        return;
    }
    set_b8(a, 0x25C, 0);                                       /* 0021D5DC */
    em_player_reaction_0017C540(a);
    set_b8(a, 0x317, 0);
}

/* 0021D600. */
int em_player_reaction_0021D600(const EmPlayerLiveActor *a)
{
    uint8_t v = b8(a, 0x1F1);
    return v == 1 || (unsigned)(v - 3) < 2;
}

/* ---- 0021D250 / 0021D2E0 / 00179880: one translation, em_player_fall.c ----
 * The fall lane owns these three routines (docs/PLAYER_FALL.md "One owner").
 * The bridge below hands the fall translation this lane's workers: the same
 * original callees (001749A0, 001B61C0, 001FBD50, 001C6DA0 with node 1's
 * +C0 / +C8 read after it, 001EFD90, 001AEDE0, 00175900) in the same order,
 * and `scratch` as the scratchpad words 0021D2E0 writes (its 001EFD90 point
 * is the vector at 0x700038A0). A missing worker refuses before any write. */
typedef struct FallBridge {
    const EmPlayerReactionWorkers *w;
    uint32_t node1[3];      /* node 1 +C0 / +C4 / +C8 as anim_eval_skeleton left them */
} FallBridge;

static int fb_request(void *c, EmPlayerLiveActor *a, int clip, int force, float blend)
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    return w->request(w->context, a, clip, force, blend);
}
static int fb_sound(void *c, EmPlayerLiveActor *a, int id)
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    return w->sound(w->context, a, (unsigned)id);
}
static int fb_rumble(void *c, int x, int y, int z, int u)
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    return w->rumble(w->context, x, y, z, u);
}
static int fb_effect(void *c, uint32_t id, const uint32_t point[4], const uint32_t at[4])
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    float position[4], rotation[4];
    memcpy(position, point, sizeof position);        /* the words, bit for bit */
    memcpy(rotation, at, sizeof rotation);
    return w->effect(w->context, id, position, rotation);
}
static int fb_fade(void *c, int x, int y)
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    return w->fade(w->context, x, y);
}
static int fb_skeleton(void *c, EmPlayerLiveActor *a)
{
    FallBridge *b = c;
    float node1[3];
    FAULT(b->w->skeleton(b->w->context, a, node1));
    memcpy(b->node1, node1, sizeof b->node1);
    return 0;
}
static int fb_hip(void *c, uint32_t *x, uint32_t *z)
{
    const FallBridge *b = c;
    *x = b->node1[0];
    *z = b->node1[2];
    return 0;
}
static int fb_floor(void *c, EmPlayerLiveActor *a, int search, int *result)
{
    const EmPlayerReactionWorkers *w = ((FallBridge *)c)->w;
    return w->floor(w->context, a, search, result);
}

/* The fall lane's worker set over this lane's workers: only the slots the
 * three routines reach, each left NULL when this lane's worker is missing
 * (em_player_fall_0021D250 / _0021D2E0 then refuse before any write). */
static void fall_bridge(FallBridge *b, const EmPlayerReactionWorkers *w, EmPlayerLandWorkers *out)
{
    memset(out, 0, sizeof *out);
    memset(b, 0, sizeof *b);
    b->w = w;
    out->context = b;
    out->scratch = w->scratch;
    if (w->request) out->request = fb_request;
    if (w->sound) out->sound = fb_sound;
    if (w->rumble) out->rumble = fb_rumble;
    if (w->effect) out->effect = fb_effect;
    if (w->fade) out->fade = fb_fade;
    if (w->skeleton) {
        out->skeleton = fb_skeleton;
        out->hip = fb_hip;
    }
    if (w->floor) out->floor = fb_floor;
}

/* 0021D250(p, a1). */
int em_player_reaction_0021D250(EmPlayerLiveActor *a, int a1, const EmPlayerReactionWorkers *w)
{
    if (!w) return -1;
    FallBridge b;
    EmPlayerLandWorkers fall;
    fall_bridge(&b, w, &fall);
    return em_player_fall_0021D250(&fall, a, a1);
}

/* 0021D2E0(p, a1, a2): the +7 countdown to 001AEDE0(4, 0). */
int em_player_reaction_0021D2E0(EmPlayerLiveActor *a, int16_t a1, int a2,
                                const EmPlayerReactionWorkers *w)
{
    if (!w) return -1;
    FallBridge b;
    EmPlayerLandWorkers fall;
    fall_bridge(&b, w, &fall);
    return em_player_fall_0021D2E0(&fall, a, a1, a2);
}

/* 00182870(p, a1): the voice id by the floor attribute +23A. */
int em_player_reaction_00182870(EmPlayerLiveActor *a, int a1, const EmPlayerReactionWorkers *w)
{
    unsigned id;
    switch (b8(a, 0x23A)) {
    case 1: id = a1 == 0 ? 0x30 : 0x31; break;
    case 2: id = a1 == 0 ? 0x41 : 0x42; break;
    case 3: id = a1 == 0 ? 0x52 : 0x53; break;
    case 4: id = a1 == 0 ? 0x63 : 0x64; break;
    case 5: id = a1 == 0 ? 0x74 : 0x75; break;
    case 0x5A:
        if (a1) return 0;
        id = 0x85;
        break;
    case 8: id = a1 == 0 ? 0x96 : 0x97; break;
    case 0x5C:
        if (a1) return 0;
        id = 0xA7;
        break;
    case 6:
    case 7: id = a1 == 0 ? 0xB8 : 0xB9; break;
    case 0x5B:
        if (a1) return 0;
        id = b8(a, 0x23C) == 1 ? 0xC9 : 0xDA;
        break;
    case 0xD: id = a1 == 0 ? 0xEB : 0xEC; break;
    case 0xE: id = a1 == 0 ? 0xFC : 0xFD; break;
    default: id = a1 == 0 ? 0x1F : 0x20; break;   /* 0 and every other value */
    }
    return sound(w, a, id);
}

/* 0021D490. */
int em_player_reaction_0021D490(EmPlayerLiveActor *a, const EmPlayerReactionWorkers *w)
{
    return sound(w, a, b8(a, 0x234) == 0 ? 0x14E : 0x14F);
}

/* 0021C120. */
int em_player_reaction_0021C120(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    set_b8(a, 0x31F, 0x3C);
    uint32_t handle;
    if (!w->attach) return -1;
    FAULT(w->attach(w->context, a, 0x80000040u, &handle));    /* result unused */
    s->d8106F0 = 1;
    FAULT(sound(w, a, 0x14D));
    return rumble(w, 1, 0xEE, 0x3C, 1);
}

/* 0021C190: *result 1 when the +31F countdown passed zero this call. */
int em_player_reaction_0021C190(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w, int *result)
{
    uint8_t v = b8(a, 0x31F);
    set_b8(a, 0x31F, v - 1);
    *result = 0;
    if (v != 0) return 0;
    if (!w->model_refresh || !w->attach || !w->stream_check) return -1;
    FAULT(w->model_refresh(w->context, a));                     /* 0015C1F0(p) */
    uint32_t handle;
    FAULT(w->attach(w->context, a, 0x80000048u, &handle));
    set_fw(a, 0x1C, handle);
    s->d8106F0 = 0;                                            /* 0021C1D4, before 001FAFD0 */
    FAULT(w->stream_check(w->context));
    *s->d8106F1 = 0;
    *result = 1;
    return 0;
}

/* 0021C270 / 0021C350: translated by lane player-stage-workers
 * (em_player_stage_workers.c); workers here. */
static int w0021C270(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a)
{
    return w->w0021C270 ? w->w0021C270(w->context, a) : -1;
}
static int w0021C350(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a)
{
    return w->w0021C350 ? w->w0021C350(w->context, a) : -1;
}

/* 0021D1A0 (instruction words): *result 1 when the hit direction (+70,
 * +78) is more than pi/2 from the body yaw +C4. The angle goes through
 * 0x70003A20: the atan2 result is stored there (0021D1C4), then
 * wrap(pi/2 + it) (0021D1E8); the final difference goes to 0x70003A24
 * (0021D204), a word the shared scratch does not hold (no first-level
 * reader). */
int em_player_reaction_0021D1A0(EmPlayerLiveActor *a, const EmPlayerReactionWorkers *w,
                                int *result)
{
    if (!w->atan2 || !w->scratch) return -1;
    uint32_t angle = sbits(w->atan2(w->context, sfloat(em_ee_neg_bits(fw(a, 0x78))),
                                    sfloat(fw(a, 0x70))));
    w->scratch->s3A20 = angle;                                         /* 0021D1C4 */
    angle = wrap_bits(em_ee_add_bits(K_HALF_PI, w->scratch->s3A20));  /* 0021D1E0 */
    w->scratch->s3A20 = angle;                                         /* 0021D1E8 */
    angle = wrap_bits(em_ee_sub_bits(angle, fw(a, 0xC4)));            /* 0021D1F8 */
    uint32_t magnitude = angle & 0x7FFFFFFFu;                          /* 0011DF78 */
    *result = em_ee_c_le_bits(magnitude, K_HALF_PI) ? 0 : 1;
    return 0;
}

/* 001754E0(p, a1): count the input in +28; *result is
 * (short)+28 >= a1. The angle's magnitude is stored at 0x70003A20 before
 * the test (decomp src/func_001754E0.c, byte-matched). */
int em_player_reaction_001754E0(EmPlayerLiveActor *a, const EmPlayerReactionScene *s,
                                EmPlayerLandScratch *scratch, int a1, int *result)
{
    if (!scratch) return -1;
    if (b8(a, 0x23F) != 0) {
        uint32_t angle = wrap_bits(em_ee_sub_bits(fw(a, 0x26C), fw(a, 0x24C)));
        uint32_t magnitude = angle & 0x7FFFFFFFu;                      /* 0011DF78 */
        scratch->s3A20 = magnitude;
        if (!em_ee_c_le_bits(magnitude, K_HALF_PI)) {
            set_h16(a, 0x28, h16(a, 0x28) + 1);
            set_b8(a, 0x2FE, 0x3C);
        }
    } else if (s->pad_pressed & s->spad3B76) {                          /* D_00810E74 */
        set_h16(a, 0x28, h16(a, 0x28) + 1);
        set_b8(a, 0x2FE, 0x3C);
    }
    *result = !(h16(a, 0x28) < a1);
    return 0;
}

/* The per-frame root-motion integration most sub-states share:
 * +38 = root8 - +21C, +21C = root8 (then the caller's scale). */
static void root_forward(EmPlayerLiveActor *a, const EmPlayerReactionScene *s)
{
    uint32_t root = sbits(s->root8);
    set_fw(a, 0x38, em_ee_sub_bits(root, fw(a, 0x21C)));
    set_fw(a, 0x21C, root);
}

/* +2EC = root4 - +2E4, +2E4 = root4 (the vertical counterpart). */
static void root_up(EmPlayerLiveActor *a, const EmPlayerReactionScene *s)
{
    uint32_t root = sbits(s->root4);
    set_fw(a, 0x2EC, em_ee_sub_bits(root, fw(a, 0x2E4)));
    set_fw(a, 0x2E4, root);
}

static void scale_speed(EmPlayerLiveActor *a, uint32_t factor)
{
    set_fw(a, 0x38, em_ee_mul_bits(fw(a, 0x38), factor));
}

static void rise(EmPlayerLiveActor *a)
{
    set_fw(a, 0xB4, em_ee_add_bits(fw(a, 0xB4), fw(a, 0x2EC)));
}

/* 00179880 then 00175900(p, 1), the tail most routines end with. */
static int drop_and_floor(const EmPlayerReactionWorkers *w, EmPlayerLiveActor *a, int *landed)
{
    em_player_fall_drop(a);
    int ignored;
    return floor_service(w, a, landed ? landed : &ignored);
}

static int flag(const EmPlayerLiveActor *a, uint32_t mask) { return (em_live_u32(a, 0x200) & mask) != 0; }

/* ---- 0021D800: +5 0 / 0x17 (instructions 0021D800..0021DBAC) ------------ */
int em_player_reaction_0021D800(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    if (st == 0) {
        set_b8(a, 6, st + 1);
        set_b8(a, 7, 0);
        set_fw(a, 0x2F4, fw(a, 0xB4));                                   /* 0021D84C */
        FAULT(rumble(w, 0, 0xC0, 5, 1));
        FAULT(sound(w, a, b8(a, 0x1F1) == 1 ? 0x153 : 0x152));
        uint32_t r;
        if (!w->random) return -1;
        FAULT(w->random(w->context, &r));
        int odd = (r & 1) != 0;
        if (b8(a, 0x236) != 0) {
            FAULT(request(w, a, odd ? 0x56 : 0x57, 0, K_0));
        } else if (b8(a, 0x234) == 0) {
            int behind;
            FAULT(em_player_reaction_0021D1A0(a, w, &behind));
            FAULT(request(w, a, odd ? (behind ? 0x1E : 0x1F) : (behind ? 0x20 : 0x21), 0, K_0));
        } else {
            FAULT(request(w, a, 0x1C7, 0, K_0));
        }
        if (!em_player_reaction_0021D600(a)) FAULT(sound(w, a, odd ? 0x146 : 0x147));
        set_fw(a, 0x38, 0);                                              /* 0021DA24 */
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2EC, 0);
    } else if (st == 1) {
        if (flag(a, 0x1000)) {
            set_h16(a, 0x20E, b8(a, 0x1F1) == 2 ? 0x5A : 0x3C);
            if (b8(a, 0x319) != 0 && b8(a, 5) == 0x17) {
                em_player_reaction_0021D530(a, s);                       /* 0021DA84 */
            } else {
                set_fw(a, 0xB4, em_ee_add_bits(fw(a, 0xB4), K_M0_2));
                int landed;
                FAULT(drop_and_floor(w, a, &landed));
                if (landed != 0) {
                    set_b8(a, 0x25C, 0);
                    em_player_reaction_0017C540(a);                      /* 0021DAD0 */
                } else {
                    set_fw(a, 0x2F4, fw(a, 0xB4));
                    set_b8(a, 4, 1);                                     /* 0021DAF4 */
                    set_b8(a, 5, 7);                                     /* 0021DAF8 */
                    set_b8(a, 6, 0);
                    set_b8(a, 0x1F0, 0xD);
                }
            }
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
            set_fw(a, 0xB4, em_ee_add_bits(fw(a, 0xB4), K_M0_2));
            int landed;
            FAULT(drop_and_floor(w, a, &landed));
            set_b8(a, 0x1F0, landed != 0 ? 0x3E : 0xD);
        }
    }
    if (b8(a, 0x23A) == 0x5D) return em_player_reaction_0021D250(a, 0, w);  /* 0021DB98 */
    return 0;
}

/* ---- 0021E240: +5 1 ------------------------------------------------------- */
int em_player_reaction_0021E240(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0:
        FAULT(rumble(w, 0, 0xC0, 5, 1));
        FAULT(sound(w, a, 0x146));
        FAULT(sound(w, a, 0x151));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        FAULT(request(w, a, b8(a, 0x236) != 0 ? 0x5C : 0x2A, 0, K_ONE));
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x38, 0);
        set_fw(a, 0x2EC, 0);
        break;
    case 1:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 7, 0);
        } else {
            uint8_t st2 = b8(a, 7);
            if (st2 == 0) {
                if (em_ee_c_le_bits(fw(a, 0x3C), K_80)) {
                    set_b8(a, 7, st2 + 1);
                    FAULT(sound(w, a, 0x156));
                }
            } else if (st2 == 1) {
                if (em_ee_c_le_bits(fw(a, 0x3C), K_50)) {
                    set_b8(a, 7, st2 + 1);
                    FAULT(em_player_reaction_00182870(a, 1, w));
                }
            } else if (st2 == 2) {
                if (em_ee_c_le_bits(fw(a, 0x3C), K_16)) {
                    set_b8(a, 7, st2 + 1);
                    FAULT(em_player_reaction_0021D490(a, w));
                    FAULT(rumble(w, 1, 0xEE, 0x3C, 1));
                }
            }
            root_forward(a, s);
            FAULT(translate(w, a, 1));
        }
        break;
    case 2:
        FAULT(em_player_reaction_0021D2E0(a, 0x78, 1, w));
        break;
    }
    return drop_and_floor(w, a, NULL);
}

/* ---- 0021E490: +5 2 / 0x18 (instructions 0021E490..0021E640) ------------- */
int em_player_reaction_0021E490(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    if (st == 0) {
        FAULT(sound(w, a, 0x153));
        FAULT(rumble(w, 0, 0xC0, 5, 1));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        FAULT(request(w, a, b8(a, 0x236) != 0 ? 0x57 : 0x1F, 0, K_ONE));
        set_fw(a, 0x2EC, 0);
    } else if (st == 1) {
        if (em_ee_c_le_bits(fw(a, 0x3C), K_22)) {
            set_b8(a, 6, st + 1);
            FAULT(em_player_reaction_0021C120(a, s, w));
        }
    } else if (st == 2) {
        int done;
        FAULT(em_player_reaction_0021C190(a, s, w, &done));
        if (done != 0) set_b8(a, 6, b8(a, 6) + 1);
        else set_fw(a, 0x204, K_0_1);
    } else if (st == 3) {
        if (flag(a, 0x1000)) {
            set_h16(a, 0x20E, 0x3C);
            if (b8(a, 5) == 0x18) {
                em_player_reaction_0021D530(a, s);                       /* 0021E5E8 */
            } else {
                set_b8(a, 0x25C, 0);
                em_player_reaction_0017C540(a);                          /* 0021E5F8 */
            }
        } else {
            set_fw(a, 0x204, K_0_25);
        }
    }
    FAULT(probes(w, a));                                                  /* 0021E614 */
    return drop_and_floor(w, a, NULL);
}

/* The hand-back of 00223C70 sub-states 2 and 0x17. */
static void release_00223C70(EmPlayerLiveActor *a, const EmPlayerReactionScene *s)
{
    if (!flag(a, 0x1000)) return;
    set_h16(a, 0x20E, 0x3C);
    set_b8(a, 4, 1);
    if (s->pad_held & s->spad3B7E) {
        set_b8(a, 5, 0x20);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x35);
        set_b8(a, 0x1F1, 0);
        set_b8(a, 0x317, 1);
    } else if (s->pad_held & s->spad3B7C) {
        set_b8(a, 5, 0x1F);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x34);
        set_b8(a, 0x1F1, 0);
        set_b8(a, 0x317, 1);
    } else {
        set_b8(a, 5, 0x14);
        set_b8(a, 6, 0);
        set_b8(a, 0x1F0, 0x26);
        set_b8(a, 0x317, 0);
    }
}

/* ---- 00223C70: +5 0xA ----------------------------------------------------- */
int em_player_reaction_00223C70(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0:
        if (b8(a, 0x1F1) == 0) {
            FAULT(rumble(w, 0, 0xC0, 5, 1));
            FAULT(sound(w, a, 0x152));
            if (em_ee_c_le_bits(fw(a, 0x220), K_0)) {
                if (b8(a, 0xF) == 0x63 || b8(a, 0x234) == 1) set_b8(a, 6, 0x1E);
                else set_b8(a, 6, 0xA);
            } else {
                set_b8(a, 6, b8(a, 6) + 1);
            }
        } else {
            FAULT(rumble(w, 0, 0xC0, 5, 1));
            FAULT(sound(w, a, 0x153));
            if (!em_ee_c_lt_bits(fw(a, 0x228), K_100) && *s->d8106F1 != 0) set_b8(a, 6, 0x14);
            else set_b8(a, 6, b8(a, 6) + 1);
        }
        break;
    case 1:
        set_b8(a, 6, b8(a, 6) + 1);
        if (b8(a, 0xF) & 2) {
            FAULT(request(w, a, 0x185, 0, 0x40800000u));           /* 4.0 */
            set_b8(a, 0xF, 0);
        } else {
            uint32_t r;
            if (!w->random) return -1;
            FAULT(w->random(w->context, &r));
            FAULT(request(w, a, (r & 1) ? 0x185 : 0x184, 0, 0x40800000u));
        }
        break;
    case 2:
    case 23:
        release_00223C70(a, s);
        break;
    case 10:
        set_b8(a, 6, b8(a, 6) + 1);
        FAULT(request(w, a, 0x187, 0, 0x41000000u));               /* 8.0 */
        break;
    case 11:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, b8(a, 6) + 1);
            set_b8(a, 7, 0);
            FAULT(rumble(w, 1, 0xEE, 0x3C, 1));
        }
        break;
    case 12:
        FAULT(em_player_reaction_0021D2E0(a, 0x78, 1, w));
        break;
    case 20:
        set_b8(a, 6, b8(a, 6) + 1);
        FAULT(request(w, a, 0x184, 0, 0x40800000u));
        break;
    case 21:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_30)) {
            set_b8(a, 6, b8(a, 6) + 1);
            FAULT(em_player_reaction_0021C120(a, s, w));
        }
        break;
    case 22: {
        int done;
        FAULT(em_player_reaction_0021C190(a, s, w, &done));
        if (done != 0) set_b8(a, 6, b8(a, 6) + 1);
        break;
    }
    case 30:
        set_b8(a, 6, b8(a, 6) + 1);
        FAULT(request(w, a, 0x186, 0, 0x41000000u));
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2E4, 0);
        set_b8(a, 0x25F, 2);
        break;
    case 31:
        if (!flag(a, 0x8000)) {
            set_b8(a, 6, b8(a, 6) + 1);
            s->d275B08 = 1;
        }
        break;
    case 32: {
        if (flag(a, 0x1000)) {
            set_b8(a, 6, b8(a, 6) + 1);
            break;
        }
        root_forward(a, s);
        FAULT(translate(w, a, 1));
        root_up(a, s);
        if (em_ee_c_lt_bits(fw(a, 0x2EC), K_M4)) set_fw(a, 0x2EC, K_M4);
        rise(a);
        int ignored;
        FAULT(floor_service(w, a, &ignored));
        break;
    }
    case 33: {
        em_player_fall_drop(a);
        int landed;
        FAULT(floor_service(w, a, &landed));
        if (landed != 0) {
            FAULT(em_player_reaction_00182870(a, 1, w));
            FAULT(sound(w, a, 0x156));
            set_b8(a, 4, 2);
            set_b8(a, 5, 3);                                          /* 00224244 */
            set_b8(a, 6, 0);
            set_b8(a, 0x1F0, 0x3F);
        } else if (b8(a, 0x23A) == 0x5D) {
            FAULT(em_player_reaction_0021D250(a, 0, w));
        }
        break;
    }
    }
    return 0;
}

/* ---- 0021F330: +5 0xB ----------------------------------------------------- */
int em_player_reaction_0021F330(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0:
        set_b8(a, 6, st + 1);
        set_b8(a, 7, 0);
        FAULT(request(w, a, 0x2C, 0, K_0));
        if (!w->heading) return -1;
        FAULT(w->heading(w->context, a, 0));
        set_fw(a, 0x26C, fw(a, 0x24C));
        set_h16(a, 0x28, 0);
        set_b8(a, 0x2FE, 0);
        set_fw(a, 0x2EC, 0);
        break;
    case 1: {
        int8_t c = (int8_t)b8(a, 0x2FE);
        set_b8(a, 0x2FE, (uint8_t)(c - 1));
        if (c == 0) {
            set_b8(a, 0x2FE, 0);
            set_h16(a, 0x28, 0);
        }
        if (!w->heading) return -1;
        FAULT(w->heading(w->context, a, 0));
        int count;
        FAULT(em_player_reaction_001754E0(a, s, w->scratch, 6, &count));
        if (count != 0 || s->scripted != 0) {
            set_b8(a, 6, b8(a, 6) + 1);
            set_b8(a, 0, b8(a, 0) | 4);
            FAULT(request(w, a, 0x2D, 0, 0x41000000u));           /* 8.0 */
            FAULT(sound(w, a, 0x15A));
            set_fw(a, 0x224, 0);
        } else if (s->d81083C == 0) {
            s->d8106BC = 0;
            set_b8(a, 0, 3);
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
        } else {
            if (!is_zero(fw(a, 0x224))) {
                FAULT(w0021C350(w, a));
                if (em_ee_c_le_bits(fw(a, 0x220), K_0)) {
                    set_b8(a, 0, 2);
                    set_b8(a, 4, 2);                                  /* 0021F514 / 0021F52C */
                    if (b8(a, 0x234) == 1) {
                        set_b8(a, 5, 3);
                        set_b8(a, 6, 0);
                        set_b8(a, 0x1F0, 0x3F);
                    } else {
                        set_b8(a, 5, 1);
                        set_b8(a, 6, 0);
                        set_b8(a, 0x1F0, 0x40);
                    }
                    return 0;                                         /* no tail */
                }
                FAULT(rumble(w, 0, 0xC0, 5, 1));
                FAULT(sound(w, a, 0x154));
                FAULT(request(w, a, 0x1E, 1, K_ONE));
                set_b8(a, 7, 1);
            } else if (!is_zero(fw(a, 0x22C))) {
                FAULT(w0021C270(w, a));
                if (!em_ee_c_lt_bits(fw(a, 0x228), K_100) && *s->d8106F1 != 0) {
                    set_b8(a, 6, 0xA);
                    s->d8106BC = 1;
                } else {
                    FAULT(rumble(w, 0, 0xC0, 5, 1));
                    FAULT(sound(w, a, 0x154));
                    FAULT(request(w, a, 0x1E, 1, K_ONE));
                    set_b8(a, 7, 1);
                }
            }
            uint8_t sub = b8(a, 7);
            if (sub == 0) {
                set_fw(a, 0x204, h16(a, 0x28) != 0 ? K_2_2 : K_0_5);
            } else if (sub == 1) {
                if (flag(a, 0x1000)) {
                    FAULT(request(w, a, 0x2C, 0, 0x40800000u));       /* 4.0 */
                    set_b8(a, 7, 0);
                } else {
                    set_fw(a, 0x204, K_1_2);
                }
            }
        }
        set_fw(a, 0x26C, fw(a, 0x24C));
        break;
    }
    case 2:
        if (!flag(a, 0x8000)) {
            set_b8(a, 6, st + 1);
            s->d8106BC = 1;
        }
        break;
    case 3:
    case 13:
        if (flag(a, 0x1000)) {
            s->d8106BC = 0;
            set_b8(a, 0, 3);
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
            set_fw(a, 0x224, 0);
            set_fw(a, 0x22C, 0);
        } else if (st == 13) {
            set_fw(a, 0x204, K_0_5);
        }
        break;
    case 10:
        set_b8(a, 6, st + 1);
        FAULT(request(w, a, 0x1C7, 0, K_ONE));
        break;
    case 11:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_60)) {
            set_b8(a, 6, st + 1);
            FAULT(em_player_reaction_0021C120(a, s, w));
        }
        break;
    case 12: {
        int done;
        FAULT(em_player_reaction_0021C190(a, s, w, &done));
        if (done != 0) set_b8(a, 6, b8(a, 6) + 1);
        else set_fw(a, 0x204, K_0_1);
        break;
    }
    }
    FAULT(probes(w, a));
    if (b8(a, 0xF) != 0) s->d8106BC = 1;
    return drop_and_floor(w, a, NULL);
}

/* ---- 0021F850: +5 0xC ----------------------------------------------------- */
int em_player_reaction_0021F850(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0:
        FAULT(sound(w, a, 0x150));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        s->d8106BC = 1;
        FAULT(request(w, a, 0x2E, 0, 0x40C00000u));                   /* 6.0 */
        set_fw(a, 0x38, 0);
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2EC, 0);
        break;
    case 1:
        if (!flag(a, 0x8000)) set_b8(a, 6, st + 1);
        break;
    case 2:
    case 3:
        if (st == 2 && em_ee_c_le_bits(fw(a, 0x3C), K_35)) {
            set_b8(a, 6, st + 1);
            FAULT(sound(w, a, 0x12F));
            FAULT(rumble(w, 0, 0xD0, 0xA, 1));
        }
        /* case 2 falls through into case 3 */
        if (flag(a, 0x1000)) {
            if (!em_ee_c_lt_bits(fw(a, 0x228), K_100) && *s->d8106F1 != 0) {
                set_b8(a, 6, b8(a, 6) + 1);
                FAULT(em_player_reaction_0021C120(a, s, w));
                set_b8(a, 0x302, 1);
            } else {
                set_b8(a, 6, b8(a, 6) + 3);
                set_b8(a, 0x302, 0);
            }
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
        }
        break;
    case 4: {
        int done;
        FAULT(em_player_reaction_0021C190(a, s, w, &done));
        if (done != 0) {
            set_b8(a, 6, b8(a, 6) + 1);
            set_h16(a, 0x28, 0x10);
        }
        break;
    }
    case 5: {
        int16_t t = h16(a, 0x28);
        set_h16(a, 0x28, t - 1);
        if (t == 0) set_b8(a, 6, b8(a, 6) + 1);
        break;
    }
    case 6:
        set_b8(a, 6, st + 1);
        FAULT(request(w, a, 0x24, 0, 0x41000000u));                   /* 8.0 */
        set_fw(a, 0x38, 0);
        set_fw(a, 0x21C, 0);
        break;
    case 7:
        if (!flag(a, 0x8000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 0x1F1, 0);
        }
        break;
    case 8:
        if (flag(a, 0x1000)) {
            set_b8(a, 0xF, 0);
            set_fw(a, 0x224, 0);
            set_fw(a, 0x22C, 0);
            s->d8106BC = 0;
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
            if (b8(a, 0x302) != 0) set_fw(a, 0x204, K_0_4);
        }
        break;
    }
    return drop_and_floor(w, a, NULL);
}

/* ---- 002202C0: +5 0xF ----------------------------------------------------- */
int em_player_reaction_002202C0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0:
        FAULT(sound(w, a, 0x150));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        FAULT(request(w, a, 0x32, 0, K_ONE));
        set_fw(a, 0x38, 0);
        set_fw(a, 0x21C, 0);
        break;
    case 1:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            int frames;
            if (!w->clip_frames || !w->arbiter || !w->scratch) return -1;
            FAULT(w->clip_frames(w->context, a, 0x26, &frames));
            /* 0x70003A20 = (float)frames (cvt.s.w), stored before the
             * arbiter call (decomp src/func_002202C0.c, the 0x26 hand-over). */
            uint32_t length = em_ee_cvt_s_w_bits((uint32_t)frames);
            w->scratch->s3A20 = length;
            FAULT(w->arbiter(w->context, a, 0x26, sfloat(K_0),
                             sfloat(em_ee_sub_bits(length, K_30))));
            set_fw(a, 0x38, 0);
            set_fw(a, 0x21C, sbits(s->root8));
            set_fw(a, 0x2E4, 0);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
            root_up(a, s);
            rise(a);
            int ignored;
            FAULT(floor_service(w, a, &ignored));
        }
        break;
    case 2:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
        } else {
            root_forward(a, s);
            scale_speed(a, K_0_75);
            FAULT(translate(w, a, 1));
            root_up(a, s);
            rise(a);
            int ignored;
            FAULT(floor_service(w, a, &ignored));
        }
        break;
    case 3: {
        FAULT(translate(w, a, 1));
        int landed;
        FAULT(drop_and_floor(w, a, &landed));
        if (landed != 0) {
            set_b8(a, 6, b8(a, 6) + 1);
            FAULT(sound(w, a, 0x12F));
            FAULT(rumble(w, 0, 0xD0, 0xA, 1));
            FAULT(request(w, a, 0x27, 0, K_0));
            set_fw(a, 0x38, 0);
            set_fw(a, 0x21C, 0);
        }
        break;
    }
    case 4:
        if (flag(a, 0x1000)) {
            if (!em_ee_c_lt_bits(fw(a, 0x228), K_100) && *s->d8106F1 != 0) {
                set_b8(a, 6, st + 1);
                FAULT(em_player_reaction_0021C120(a, s, w));
                set_b8(a, 0x302, 1);
            } else {
                set_b8(a, 6, b8(a, 6) + 2);
                set_b8(a, 0x302, 0);
            }
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 5: {
        int done;
        FAULT(em_player_reaction_0021C190(a, s, w, &done));
        if (done != 0) set_b8(a, 6, b8(a, 6) + 1);
        FAULT(drop_and_floor(w, a, NULL));
        break;
    }
    case 6:
        if (!is_zero(fw(a, 0x220))) {
            set_b8(a, 6, st + 1);
            FAULT(request(w, a, 0x28, 0, 0x41400000u));               /* 12.0 */
            set_fw(a, 0x38, 0);
            set_fw(a, 0x21C, 0);
        } else {
            set_b8(a, 6, 0xA);
            FAULT(request(w, a, 0x29, 0, K_16));
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 7:
        if (!flag(a, 0x8000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 0x1F1, 0);
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 8:
        if (flag(a, 0x1000)) {
            set_b8(a, 0xF, 0);
            set_fw(a, 0x224, 0);
            set_fw(a, 0x22C, 0);
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
            if (b8(a, 0x302) != 0) set_fw(a, 0x204, K_0_4);
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 10:
        if (!flag(a, 0x8000)) set_b8(a, 6, st + 1);
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 11:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_24)) {
            set_b8(a, 6, st + 1);
            FAULT(em_player_reaction_0021D490(a, w));
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 12:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 7, 0);
            FAULT(rumble(w, 1, 0xEE, 0x3C, 1));
        }
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 13:
        FAULT(em_player_reaction_0021D2E0(a, 0x78, 0, w));
        break;
    }
    if (b8(a, 0x23A) == 0x5D) return em_player_reaction_0021D250(a, 0, w);
    return 0;
}

/* ---- 0021DBB0: +5 0x10 ---------------------------------------------------- */
int em_player_reaction_0021DBB0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0: {
        FAULT(sound(w, a, 0x152));
        FAULT(sound(w, a, 0x148));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        int behind;
        FAULT(em_player_reaction_0021D1A0(a, w, &behind));
        if (behind != 0) {
            set_b8(a, 0xD, 1);
            FAULT(request(w, a, b8(a, 0x236) != 0 ? 0x5A : 0x26, 0, K_ONE));
        } else {
            set_b8(a, 0xD, 0);
            FAULT(request(w, a, b8(a, 0x236) != 0 ? 0x58 : 0x22, 0, K_ONE));
        }
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2E4, 0);
        set_fw(a, 0x2EC, 0);
        FAULT(probes(w, a));
        break;
    }
    case 1: {
        if (flag(a, 0x1000)) {
            set_b8(a, 6, b8(a, 0x236) != 0 ? st + 2 : st + 1);
            FAULT(probes(w, a));
        } else {
            root_forward(a, s);
            scale_speed(a, K_0_45);
            FAULT(translate(w, a, 1));
            root_up(a, s);
            rise(a);
        }
        int ignored;
        FAULT(floor_service(w, a, &ignored));
        break;
    }
    case 2: {
        FAULT(translate(w, a, 1));
        int landed;
        FAULT(drop_and_floor(w, a, &landed));
        if (landed != 0) {
            set_b8(a, 6, b8(a, 6) + 1);
            FAULT(request(w, a, b8(a, 0xD) == 0 ? 0x23 : 0x27, 0, K_0));
            set_fw(a, 0x21C, 0);
            FAULT(sound(w, a, 0x12F));
            FAULT(rumble(w, 0, 0xD0, 0xA, 1));
        }
        break;
    }
    case 3:
        if (flag(a, 0x1000)) {
            if (em_ee_c_le_bits(fw(a, 0x220), K_0)) {
                set_b8(a, 6, 0xA);
                FAULT(request(w, a, b8(a, 0xD) == 0 ? 0x25 : 0x29, 0, 0x40800000u));
            } else {
                set_b8(a, 6, st + 1);
                int crouched = b8(a, 0x236) != 0;
                int clip = b8(a, 0xD) == 0 ? (crouched ? 0x59 : 0x24) : (crouched ? 0x5B : 0x28);
                FAULT(request(w, a, clip, 0, crouched ? K_16 : 0x40800000u));
            }
            set_fw(a, 0x21C, 0);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 0));
        }
        FAULT(probes(w, a));
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 4:
        if (!flag(a, 0x8000)) set_b8(a, 6, st + 1);
        FAULT(probes(w, a));
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 5:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_10)) {
            set_b8(a, 6, st + 1);
            FAULT(sound(w, a, 0x151));
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 0));
        }
        FAULT(probes(w, a));
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 6:
        if (flag(a, 0x1000)) {
            set_b8(a, 0xF, 0);
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 0));
        }
        FAULT(probes(w, a));
        FAULT(drop_and_floor(w, a, NULL));
        break;
    case 10:
        if (!flag(a, 0x8000)) {
            set_b8(a, 6, st + 1);
            FAULT(sound(w, a, 0x156));
        }
        break;
    case 11:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_18)) {
            set_b8(a, 6, st + 1);
            FAULT(em_player_reaction_0021D490(a, w));
        }
        break;
    case 12:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 7, 0);
            set_fw(a, 0x2EC, 0);
            FAULT(rumble(w, 1, 0xEE, 0x3C, 1));
        }
        break;
    case 13:
        FAULT(em_player_reaction_0021D2E0(a, 0x78, 0, w));
        break;
    }
    if (b8(a, 6) < 0xA && b8(a, 0x23A) == 0x5D) return em_player_reaction_0021D250(a, 1, w);
    return 0;
}

/* ---- 0021E9C0: +5 0x11 ---------------------------------------------------- */
int em_player_reaction_0021E9C0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    switch (b8(a, 6)) {
    case 0:
        FAULT(sound(w, a, 0x154));
        FAULT(rumble(w, 0, 0xC0, 5, 1));
        set_b8(a, 6, b8(a, 6) + 1);
        set_b8(a, 7, 0);
        FAULT(request(w, a, 0x20, 0, K_ONE));
        set_fw(a, 0x38, 0);
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2EC, 0);
        break;
    case 1:
        if (flag(a, 0x1000)) {
            set_b8(a, 0xF, 0);
            set_h16(a, 0x20E, 0x3C);
            set_b8(a, 0x25C, 0);
            em_player_reaction_0017C540(a);
        } else {
            root_forward(a, s);
            FAULT(translate(w, a, 1));
        }
        break;
    }
    return drop_and_floor(w, a, NULL);
}

/* The sub-states 0021EAD0 and 0021EF30 share (0..3). *tail says whether
 * the caller's sub-state takes 0021EAD0's drop/floor tail. */
static int blast_common(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                        const EmPlayerReactionWorkers *w, uint8_t st)
{
    switch (st) {
    case 0: {
        FAULT(sound(w, a, 0x159));
        FAULT(rumble(w, 1, 0xEE, 0x3C, 1));
        set_b8(a, 6, b8(a, 6) + 1);
        int behind;
        FAULT(em_player_reaction_0021D1A0(a, w, &behind));
        if (behind != 0) {
            set_b8(a, 0xD, 1);
            FAULT(request(w, a, 0x26, 0, K_ONE));
        } else {
            set_b8(a, 0xD, 0);
            FAULT(request(w, a, 0x22, 0, K_ONE));
        }
        set_fw(a, 0x21C, 0);
        set_fw(a, 0x2E4, 0);
        set_fw(a, 0x2EC, 0);
        return 0;
    }
    case 1: {
        set_b8(a, 6, st + 1);
        /* 0x700038B0 = (0, wrap(pi + +C4), 0, 1.0), rotation of three effects. */
        float rotation[4] = { sfloat(K_0),
                              sfloat(wrap_bits(em_ee_add_bits(K_PI, fw(a, 0xC4)))),
                              sfloat(K_0), sfloat(K_ONE) };
        if (!w->effect) return -1;
        FAULT(w->effect(w->context, 0x80000023u, s->node7, rotation));
        FAULT(w->effect(w->context, 0x80000023u, s->node2, rotation));
        FAULT(w->effect(w->context, 0x80000023u, s->node3, rotation));
    }
        /* falls through into case 2 */
    case 2: {
        if (flag(a, 0x1000)) {
            set_b8(a, 6, b8(a, 6) + 1);
            return 0;
        }
        root_forward(a, s);
        scale_speed(a, K_0_75);
        FAULT(translate(w, a, 1));
        root_up(a, s);
        rise(a);
        int ignored;
        return floor_service(w, a, &ignored);
    }
    case 3: {
        FAULT(translate(w, a, 1));
        int landed;
        FAULT(drop_and_floor(w, a, &landed));
        if (landed != 0) {
            set_b8(a, 6, b8(a, 6) + 1);
            FAULT(request(w, a, b8(a, 0xD) == 0 ? 0x23 : 0x27, 0, K_0));
            set_fw(a, 0x21C, 0);
            set_fw(a, 0x2E4, 0);
            FAULT(sound(w, a, 0x12F));
        }
        return 0;
    }
    }
    return 0;
}

/* ---- 0021EAD0: +5 0x12 / 0x13 --------------------------------------------- */
int em_player_reaction_0021EAD0(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0: case 1: case 2: case 3:
        return blast_common(a, s, w, st);
    case 4:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            FAULT(request(w, a, b8(a, 0xD) == 0 ? 0x25 : 0x29, 0, 0x40800000u));
        } else {
            root_forward(a, s);
            scale_speed(a, K_0_5);
            FAULT(translate(w, a, 1));
        }
        return drop_and_floor(w, a, NULL);
    case 5:
        if (!flag(a, 0x8000)) set_b8(a, 6, st + 1);
        return drop_and_floor(w, a, NULL);
    case 6:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_18)) {
            set_b8(a, 6, st + 1);
            FAULT(em_player_reaction_0021D490(a, w));
        }
        return drop_and_floor(w, a, NULL);
    case 7:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 7, 0);
        }
        return drop_and_floor(w, a, NULL);
    case 8:
        return em_player_reaction_0021D2E0(a, 0x78, 0, w);
    }
    return 0;
}

/* ---- 0021EF30: +5 0x14 ---------------------------------------------------- */
int em_player_reaction_0021EF30(EmPlayerLiveActor *a, EmPlayerReactionScene *s,
                                const EmPlayerReactionWorkers *w)
{
    uint8_t st = b8(a, 6);
    switch (st) {
    case 0: case 1: case 2: case 3:
        return blast_common(a, s, w, st);
    case 4:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            return request(w, a, b8(a, 0xD) == 0 ? 0x25 : 0x29, 0, 0x40800000u);
        }
        root_forward(a, s);
        scale_speed(a, K_0_5);
        return translate(w, a, 1);
    case 5:
        if (!flag(a, 0x8000)) set_b8(a, 6, st + 1);
        return 0;
    case 6:
        if (em_ee_c_le_bits(fw(a, 0x3C), K_18)) {
            set_b8(a, 6, st + 1);
            return em_player_reaction_0021D490(a, w);
        }
        return 0;
    case 7:
        if (flag(a, 0x1000)) {
            set_b8(a, 6, st + 1);
            set_b8(a, 7, 0);
        }
        return 0;
    case 8:
        return em_player_reaction_0021D2E0(a, 0x78, 0, w);
    }
    return 0;
}

/* ---- the live binding ----------------------------------------------------- */

int em_player_reaction_workers_bound(const EmPlayerReactionWorkers *w)
{
    return w && w->scratch && w->request && w->arbiter && w->clip_frames && w->sound && w->rumble &&
           w->random && w->effect && w->attach && w->floor && w->translate &&
           w->probes && w->heading && w->skeleton && w->fade && w->model_refresh &&
           w->stream_check && w->atan2 && w->w0021C270 && w->w0021C350;
}

/* The adapters for lane player-major2-states' workers. */
static const EmPlayerReaction *helper_binding(void *context)
{
    const EmPlayerReaction *b = context;
    if (!b || !b->scene || !b->scene->d8106F1 || !b->refresh ||
        !em_player_reaction_workers_bound(&b->workers))
        return NULL;
    return b->refresh(b->refresh_context, b->scene) < 0 ? NULL : b;
}

int em_player_reaction_w0021D2E0(void *context, EmPlayerLiveActor *actor, int a1, int a2)
{
    const EmPlayerReaction *b = actor ? helper_binding(context) : NULL;
    if (!b) return -1;
    return em_player_reaction_0021D2E0(actor, (int16_t)a1, a2, &b->workers) < 0 ? -1 : 0;
}

int em_player_reaction_w0021D250(void *context, EmPlayerLiveActor *actor, int a1)
{
    const EmPlayerReaction *b = actor ? helper_binding(context) : NULL;
    if (!b) return -1;
    return em_player_reaction_0021D250(actor, a1, &b->workers) < 0 ? -1 : 0;
}

int em_player_reaction_w0021D490(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerReaction *b = actor ? helper_binding(context) : NULL;
    if (!b) return -1;
    return em_player_reaction_0021D490(actor, &b->workers) < 0 ? -1 : 0;
}

int em_player_reaction_w0021C120(void *context, EmPlayerLiveActor *actor)
{
    const EmPlayerReaction *b = actor ? helper_binding(context) : NULL;
    if (!b) return -1;
    return em_player_reaction_0021C120(actor, b->scene, &b->workers) < 0 ? -1 : 0;
}

int em_player_reaction_w0021C190(void *context, EmPlayerLiveActor *actor, int *result)
{
    const EmPlayerReaction *b = actor && result ? helper_binding(context) : NULL;
    if (!b) return -1;
    return em_player_reaction_0021C190(actor, b->scene, &b->workers, result) < 0 ? -1 : 0;
}

typedef int (*ReactionRoutine)(EmPlayerLiveActor *, EmPlayerReactionScene *,
                               const EmPlayerReactionWorkers *);

static int live_run(void *context, EmPlayerLiveActor *actor, ReactionRoutine routine)
{
    const EmPlayerReaction *b = actor ? helper_binding(context) : NULL;
    if (!b) return -1;
    return routine(actor, b->scene, &b->workers) < 0 ? -1 : 0;
}

#define LIVE(address) \
    int em_player_reaction_live_##address(void *context, EmPlayerLiveActor *actor) \
    { return live_run(context, actor, em_player_reaction_##address); }

LIVE(0021D800)
LIVE(0021E240)
LIVE(0021E490)
LIVE(00223C70)
LIVE(0021F330)
LIVE(0021F850)
LIVE(002202C0)
LIVE(0021DBB0)
LIVE(0021E9C0)
LIVE(0021EAD0)
LIVE(0021EF30)
