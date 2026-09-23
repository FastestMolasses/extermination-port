/* em_player_fall.c - the player's fall and landing states (see em_player_fall.h).
 *
 * Read from the original instructions (build/asm of the decomp), not from the
 * readable decompilation alone: 00162DB0 and 0017C580 are NEARMISS C, and
 * 001639E0 / 00163B40 / 00163C10 / 00163D50 / 00224290 are asm-word files.
 * Two places where the NEARMISS 00162DB0 C differs from its instructions:
 *   - both 0017D080 exits of sub-state 0 (00163004 and 001630E4) continue to
 *     the shared tail at 00163134/00163138 (+2F4 = +B4, +25F = 2); the C
 *     returns early;
 *   - sub-state 0xB loads +38 and +2E0 after 00224290 returns (0016336C),
 *     the C before the call.
 * Every address in a comment is the original instruction translated there.
 * tools/test_player_fall_reference.py executes the original instructions and
 * compares every actor byte, scratch word, return value and worker call. */
#include "game/em_player_fall.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

#define FAULT(expr) do { if ((expr) < 0) return -1; } while (0)

/* Float constants as the instructions build them (lui/ori). */
#define F_ZERO      UINT32_C(0x00000000)
#define F_1_8       UINT32_C(0x3FE66666)
#define F_0_8       UINT32_C(0x3F4CCCCD)
#define F_12        UINT32_C(0x41400000)
#define F_15        UINT32_C(0x41700000)
#define F_20_5      UINT32_C(0x41A40000)
#define F_PI        UINT32_C(0x40490FDB)
#define F_60        UINT32_C(0x42700000)
#define F_10        UINT32_C(0x41200000)
#define F_ANGLE     UINT32_C(0x3D8EFA35)   /* 0.06981317 (4 degrees) */
#define F_MINUS_0_2 UINT32_C(0xBE4CCCCD)
#define F_40        UINT32_C(0x42200000)
#define F_38        UINT32_C(0x42180000)
#define F_0_1       UINT32_C(0x3DCCCCCD)
#define F_0_25      UINT32_C(0x3E800000)
#define F_5         UINT32_C(0x40A00000)
#define F_100       UINT32_C(0x42C80000)
#define F_M104      UINT32_C(0xC2D00000)
#define F_M50       UINT32_C(0xC2480000)
#define F_M14_5     UINT32_C(0xC1680000)
#define F_M0_04     UINT32_C(0xBD23D70A)
#define F_M4        UINT32_C(0xC0800000)

/* D_002488B0 (24.0) and D_0024889C (0.022727273). */
#define D_002488B0 UINT32_C(0x41C00000)
#define D_0024889C UINT32_C(0x3CBA2E8C)
/* D_00248560 (speed) and D_00248570 (initial drop) by tier 0..3. */
static const uint32_t kFallSpeed[4] = {
    UINT32_C(0x00000000), UINT32_C(0x3E4CCCCD), UINT32_C(0x3E99999A), UINT32_C(0x3F000000)
};
static const uint32_t kFallDrop[4] = {
    UINT32_C(0x00000000), UINT32_C(0xBE4CCCCD), UINT32_C(0xBE4CCCCD), UINT32_C(0xBECCCCCD)
};
/* D_00248580: the three edge probe points in actor space (x, y, z, 1). */
static const uint32_t kEdgeProbe[3][4] = {
    { UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x40D80000), UINT32_C(0x3F800000) },
    { UINT32_C(0xC0900000), UINT32_C(0x00000000), UINT32_C(0x40D80000), UINT32_C(0x3F800000) },
    { UINT32_C(0x40900000), UINT32_C(0x00000000), UINT32_C(0x40D80000), UINT32_C(0x3F800000) },
};

static uint32_t w32(const EmPlayerLiveActor *a, unsigned at) { return em_live_u32(a, at); }
static void put32(EmPlayerLiveActor *a, unsigned at, uint32_t v) { em_live_set_u32(a, at, v); }
static uint8_t b8(const EmPlayerLiveActor *a, unsigned at) { return em_live_u8(a, at); }
static void put8(EmPlayerLiveActor *a, unsigned at, unsigned v) { em_live_set_u8(a, at, (uint8_t)v); }

int em_player_fall_workers_bound(const EmPlayerLandWorkers *w)
{
    return w && w->scratch && w->request && w->arbiter && w->clip_frames && w->sound &&
           w->rumble && w->effect && w->fade && w->skeleton && w->hip && w->heading &&
           w->test_001755B0 && w->test_0017D080 && w->test_0017F320 && w->test_0021C190 &&
           w->pose_clip && w->wrap && w->approach && w->trs && w->apply && w->floor_query &&
           w->land_sound && w->translate && w->probes && w->floor && w->fall_check &&
           w->ledge && w->reentry && w->handoff && w->react_0021C120 && w->react_0021C350 &&
           w->react_0021C270 && w->convert_00128350 && w->test_001000E0 && w->progress_8106F1;
}

/* ---- 00179880 ----------------------------------------------------------- */

void em_player_fall_drop(EmPlayerLiveActor *a)
{
    uint32_t drop = em_ee_add_bits(w32(a, 0x2EC), F_M0_04);           /* 00179894 */
    put32(a, 0x2EC, drop);                                             /* 001798AC */
    if (em_ee_c_lt_bits(drop, F_M4))                                   /* 001798A0 */
        put32(a, 0x2EC, F_M4);                                         /* 001798B0 */
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), w32(a, 0x2EC)));       /* 001798C0 */
    put8(a, 0x25F, 2);                                                 /* 001798CC */
}

/* ---- shared worker calls ------------------------------------------------ */

static int request(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int clip, int force,
                   float blend)
{
    return w->request(w->context, a, clip, force, blend);
}

static int sound(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int id)
{
    return w->sound(w->context, a, id);
}

static int floor_service(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int *result)
{
    return w->floor(w->context, a, 1, result);
}

/* The skeleton effect of 00163E90 sub-state 13 (00164108..00164170) and
 * 0021D2E0 sub-state 0 (0021D350..0021D3B4): anim_eval_skeleton, then the
 * 0x700038A0 point (node x, 0.1 + +250, node z, 1.0) and 001EFD90. */
static int skeleton_effect(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    FAULT(w->skeleton(w->context, a));
    uint32_t x = 0, z = 0;
    FAULT(w->hip(w->context, &x, &z));
    uint32_t *point = w->scratch->s38A0;
    point[0] = x;
    point[2] = z;
    point[1] = em_ee_add_bits(F_0_1, w32(a, 0x250));
    point[3] = EM_EE_ONE;
    uint32_t at[4];
    for (unsigned i = 0; i < 4; ++i) at[i] = w32(a, 0xB0 + 4 * i);
    return w->effect(w->context, UINT32_C(0x80000043), point, at);
}

/* The walk hand-back shared by the landing sub-states: 00174AC0(p, 0), then
 * 0017C440(p, 0) at gait 2/3 or 0017C540(p) (00163C98.. and siblings). */
static int walk_back(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    int ignored = 0;
    FAULT(w->heading(w->context, a, 0, &ignored));
    if (b8(a, 0x23F) >= 2) {
        put8(a, 7, b8(a, 7) + 1);
        return w->reentry(w->context, a, 0);
    }
    put8(a, 0x25C, 0);
    return w->handoff(w->context, a);
}

/* The translation until the stop clip releases its blend (00163CE4..). */
static int walk_out(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    FAULT(w->translate(w->context, a, 0));
    if (!(w32(a, 0x200) & 0x8000)) return w->handoff(w->context, a);
    return 0;
}

/* The standing tail: 001764E0, +B4 += -0.2, 00175900(p, 1), 001796C0. */
static int stand_tail(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    int ignored = 0;
    FAULT(w->probes(w->context, a, EM_PLAYER_LAND_S1_CALLER));
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), F_MINUS_0_2));
    FAULT(floor_service(w, a, &ignored));
    return w->fall_check(w->context, a);
}

/* The tail of 00163E90 and 001643B0: 001764E0, then while +302 is set the
 * drop and the floor service, else the standing tail. */
static int held_tail(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    int ignored = 0;
    FAULT(w->probes(w->context, a, EM_PLAYER_LAND_S1_CALLER));
    if (b8(a, 0x302) != 0) {
        em_player_fall_drop(a);
        return floor_service(w, a, &ignored);
    }
    put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), F_MINUS_0_2));
    FAULT(floor_service(w, a, &ignored));
    return w->fall_check(w->context, a);
}

/* ---- 00224290 ----------------------------------------------------------- */

static int land_check(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int *result)
{
    uint8_t sub = b8(a, 7);
    *result = 1;
    if (sub == 1) {                                                    /* 002243AC */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 7, 0);
            FAULT(request(w, a, 0x72, 0, 1.0f));
            em_live_set_u16(a, 0x20E, 0x3C);                          /* 002243D8 */
        }
        return 0;
    }
    if (sub != 0) return 0;                                            /* 002242B4 */
    uint32_t hit = w32(a, 0x224);
    if (em_ee_c_eq_bits(hit, F_ZERO) && em_ee_c_eq_bits(w32(a, 0x22C), F_ZERO)) {
        *result = 0;                                                   /* 002242E4 */
        return 0;
    }
    if (!em_ee_c_eq_bits(hit, F_ZERO)) {                               /* 002242F4 */
        FAULT(sound(w, a, 0x152));
        FAULT(w->react_0021C350(w->context, a));
    }
    if (!em_ee_c_eq_bits(w32(a, 0x22C), F_ZERO)) {                     /* 00224330 */
        FAULT(sound(w, a, 0x153));
        FAULT(w->react_0021C270(w->context, a));
    }
    put8(a, 7, b8(a, 7) + 1);                                          /* 0022437C */
    FAULT(w->rumble(w->context, 0, 0xC0, 5, 1));
    return request(w, a, 0x76, 0, 1.0f);
}

/* ---- 0021D250 ----------------------------------------------------------- */

static int surface5d(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int arg)
{
    put8(a, 0, 2);
    put32(a, 0x220, 0);
    put8(a, 4, 2);
    put8(a, 5, 0x16);                                                  /* 0021D270 */
    put8(a, 6, 0);
    put8(a, 7, 0);
    put8(a, 0x1F0, 0xE);
    if (arg == 0) FAULT(request(w, a, 0x72, 0, 8.0f));                 /* 0021D284 */
    FAULT(w->rumble(w->context, 1, 0xEE, 0x3C, 1));
    return sound(w, a, 0x159);
}

/* ---- 0021D2E0 ----------------------------------------------------------- */

static int teleport(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int frames, int hold)
{
    uint8_t sub = b8(a, 7);
    if (sub == 0) {                                                    /* 0021D324 */
        put8(a, 7, sub + 1);
        put8(a, 0, 2);
        em_live_set_u16(a, 0x28, (uint16_t)frames);
        put32(a, 0x220, 0);
        if (b8(a, 0x25F) == 0 && !(em_live_u16(a, 0x300) & 0x8000))
            FAULT(skeleton_effect(w, a));
    } else if (sub == 1) {                                             /* 0021D3C4 */
        if (b8(a, 0x1F0) == 0xE && b8(a, 0x319) != 0)
            FAULT(request(w, a, 0x2B, 0, 1.0f));
        int16_t count = (int16_t)em_live_u16(a, 0x28);
        em_live_set_u16(a, 0x28, (uint16_t)(count - 1));
        if (count == 0) {
            put8(a, 7, b8(a, 7) + 1);
            if (b8(a, 0xF) != 0xB) FAULT(w->fade(w->context, 4, 0));
        }
    }
    if (hold == 0) {                                                   /* 0021D42C */
        int result = 0;
        put32(a, 0xB4, em_ee_add_bits(w32(a, 0xB4), F_MINUS_0_2));
        em_player_fall_drop(a);
        FAULT(floor_service(w, a, &result));
        if (result != 0) put32(a, 0x2EC, 0);                           /* 0021D46C */
    }
    return 0;
}

/* ---- 0017C580 ----------------------------------------------------------- */

static int reset_to_reaction(EmPlayerLiveActor *a)                      /* 0017C5F4 */
{
    put8(a, 0, 2);
    put8(a, 4, 2);
    put8(a, 5, 3);
    put8(a, 6, 0);
    put8(a, 0x1F0, 0x3F);
    return 0;
}

static int land(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    put8(a, 5, 8);                                                     /* 0017C590 */
    put8(a, 6, 0);
    put8(a, 0x1F0, 0xF);
    FAULT(w->land_sound(w->context, a, 1));
    put8(a, 0x25C, 0);
    put32(a, 0x38, 0);
    if (b8(a, 0xF) == 0x63) return reset_to_reaction(a);               /* 0017C5BC */
    int value = 0, flag = 0;
    FAULT(w->convert_00128350(w->context, w32(a, 0x220), &value));
    FAULT(w->test_001000E0(w->context, value, 0, &flag));
    if (flag != 0 && b8(a, 0x234) == 1) return reset_to_reaction(a);   /* 0017C5E8 */

    uint32_t drop = em_ee_sub_bits(w32(a, 0xB4), w32(a, 0x2F4));       /* 0017C628 */
    w->scratch->s3A20 = drop;                                          /* 0017C638 */
    if (em_ee_c_lt_bits(drop, F_M104)) {                               /* 0017C62C */
        put8(a, 6, 1);
        FAULT(w->rumble(w->context, 1, 0xEE, 0x3C, 1));
        put8(a, 0, 2);
        put32(a, 0x220, 0);
        put8(a, 0x25F, 0);
        FAULT(sound(w, a, 0x151));
        FAULT(request(w, a, 0x2B, 0, 1.0f));
        put8(a, 0x1F0, 0x40);
        put8(a, 7, 0);
        return 0;
    }
    int ignored = 0;
    FAULT(w->heading(w->context, a, 0, &ignored));                     /* 0017C6A4 */
    if (em_ee_c_le_bits(w32(a, 0x220), F_ZERO)) {                      /* 0017C6B8 */
        put8(a, 6, 3);
        put8(a, 7, 0);
        return 0;
    }
    if (!em_ee_c_lt_bits(w32(a, 0x228), F_100)) {                      /* 0017C6E4 */
        uint8_t progress = 0;
        FAULT(w->progress_8106F1(w->context, &progress));
        if (progress != 0) {
            put8(a, 6, 0xA);
            put8(a, 7, 0);
            return 0;
        }
    }
    drop = w->scratch->s3A20;                                          /* 0017C714 reload */
    if (em_ee_c_le_bits(drop, F_M50)) {                                /* 0017C724 */
        if (b8(a, 0x23F) == 3) {
            int blocked = 0;
            FAULT(w->test_001755B0(w->context, a, &blocked));
            if (blocked == 0) {
                put8(a, 6, 2);
                FAULT(sound(w, a, 0x13D));
                put8(a, 7, 0);
                return 0;
            }
        }
        put8(a, 6, 3);                                                 /* 0017C78C */
        FAULT(w->rumble(w->context, 0, 0xD0, 0xA, 1));
        put32(a, 0x224, F_5);
        FAULT(w->react_0021C350(w->context, a));
        FAULT(sound(w, a, 0x151));
        put8(a, 7, 0);
        return 0;
    }
    if (em_ee_c_le_bits(drop, F_M14_5)) {                              /* 0017C7D4 */
        if (b8(a, 0x23F) == 3) {
            int blocked = 0;
            FAULT(w->test_001755B0(w->context, a, &blocked));
            if (blocked == 0) {
                put8(a, 6, 2);
                FAULT(sound(w, a, 0x13D));
                put8(a, 7, 0);
                return 0;
            }
        }
        put8(a, 6, 4);                                                 /* 0017C834 */
        put8(a, 7, 0);
        return 0;
    }
    put8(a, 6, 5);                                                     /* 0017C83C */
    put8(a, 7, 0);
    return 0;
}

/* ---- the landing sub-states (00163B40 by +6) ---------------------------- */

/* 00163C10 (clip 0x6E) and 00163D50 (clip 0x6D): the same body. */
static int land_step(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int clip)
{
    uint8_t sub = b8(a, 7);
    switch (sub) {
    case 0:                                                            /* 00163C58 */
        put8(a, 7, sub + 1);
        FAULT(request(w, a, clip, 0, 1.0f));
        break;
    case 1:                                                            /* 00163C78 */
        if (w32(a, 0x200) & 0x1000) put8(a, 7, sub + 1);
        break;
    case 2:                                                            /* 00163C98 */
        FAULT(walk_back(w, a));
        break;
    case 3:                                                            /* 00163CE4 */
        FAULT(walk_out(w, a));
        break;
    default:
        break;
    }
    return stand_tail(w, a);
}

/* 00164220 (+6 = 2). */
static int land_roll(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    switch (sub) {
    case 0:                                                            /* 00164268 */
        put8(a, 7, sub + 1);
        FAULT(request(w, a, 0x74, 0, 1.0f));
        put32(a, 0x38, F_0_8);
        break;
    case 1:                                                            /* 00164290 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 7, sub + 1);
        } else {
            uint32_t speed = em_ee_sub_bits(w32(a, 0x38), D_0024889C); /* 001642C0 */
            put32(a, 0x38, speed);
            if (em_ee_c_le_bits(speed, F_ZERO)) put32(a, 0x38, 0);    /* 001642D4 */
            FAULT(w->translate(w->context, a, 0));
        }
        break;
    case 2:
        FAULT(walk_back(w, a));                                        /* 001642F0 */
        break;
    case 3:
        FAULT(walk_out(w, a));                                         /* 0016433C */
        break;
    default:
        break;
    }
    return stand_tail(w, a);
}

/* 00163E90 (+6 = 3), through jtbl_0026D600 (+7 = 5..9, 15 and >= 16 do
 * nothing). */
static int land_hurt(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    switch (sub) {
    case 0:                                                            /* 00163EC8 */
        put8(a, 7, b8(a, 7) + 1);
        FAULT(request(w, a, 0x75, 0, 1.0f));
        put8(a, 0x302, 1);
        put32(a, 0x2EC, 0);
        break;
    case 1:                                                            /* 00163EFC */
        if (!em_ee_c_le_bits(w32(a, 0x3C), F_40)) break;
        if (em_ee_c_le_bits(w32(a, 0x220), F_ZERO)) {                  /* 00163F28 */
            if (b8(a, 0x234) == 1) {
                put8(a, 4, 2);
                put8(a, 5, 3);
                put8(a, 6, 0);
                put8(a, 0x1F0, 0x3F);
            } else {
                put8(a, 7, 0xA);
                put8(a, 0x1F0, 0x40);
            }
        } else {
            put8(a, 7, sub + 1);                                       /* 00163F80 */
            em_live_set_u16(a, 0x20E, 0x3C);
            put8(a, 0x302, 0);
        }
        break;
    case 2:                                                            /* 00163F94 */
        if (w32(a, 0x200) & 0x1000) put8(a, 7, sub + 1);
        break;
    case 3:
        FAULT(walk_back(w, a));                                        /* 00163FB0 */
        break;
    case 4:
        FAULT(walk_out(w, a));                                         /* 00163FFC */
        break;
    case 10:                                                           /* 00164024 */
        put8(a, 7, sub + 1);
        FAULT(request(w, a, 0x1C3, 0, 8.0f));
        break;
    case 11:                                                           /* 00164048 */
        if (!(w32(a, 0x200) & 0x8000)) put8(a, 7, sub + 1);
        break;
    case 12:                                                           /* 00164064 */
        if (em_ee_c_le_bits(w32(a, 0x3C), F_38)) {
            FAULT(w->rumble(w->context, 1, 0xEE, 0x3C, 1));
            FAULT(sound(w, a, b8(a, 0x234) == 1 ? 0x14F : 0x14E));
            put8(a, 7, b8(a, 7) + 1);
        }
        break;
    case 13:                                                           /* 001640F0 */
        if (w32(a, 0x200) & 0x1000) {
            put8(a, 7, sub + 1);
            em_live_set_u16(a, 0x28, 0x78);
            FAULT(skeleton_effect(w, a));
        }
        break;
    case 14: {                                                         /* 00164180 */
        int16_t count = (int16_t)em_live_u16(a, 0x28);
        em_live_set_u16(a, 0x28, (uint16_t)(count - 1));
        if (count == 0) {
            put8(a, 7, b8(a, 7) + 1);
            FAULT(w->fade(w->context, 4, 0));
        }
        break;
    }
    default:
        break;
    }
    return held_tail(w, a);
}

/* 001643B0 (+6 = 0xA), through jtbl_0026D640 (+7 >= 6 does nothing). */
static int land_heavy(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 7);
    switch (sub) {
    case 0:                                                            /* 001643E8 */
        put8(a, 7, b8(a, 7) + 1);
        FAULT(request(w, a, 0x75, 0, 1.0f));
        put8(a, 0x302, 1);
        put32(a, 0x2EC, 0);
        break;
    case 1:                                                            /* 0016441C */
        if (!em_ee_c_le_bits(w32(a, 0x3C), F_40)) break;
        FAULT(w->rumble(w->context, 1, 0xEE, 0x3C, 1));
        if (!em_ee_c_le_bits(w32(a, 0x220), F_60)) put32(a, 0x220, F_60);
        FAULT(w->react_0021C120(w->context, a));
        put8(a, 7, b8(a, 7) + 1);
        break;
    case 2: {                                                          /* 00164490 */
        int done = 0;
        FAULT(w->test_0021C190(w->context, a, &done));
        if (done != 0) {
            put8(a, 7, b8(a, 7) + 1);
            put8(a, 0x302, 0);
        } else {
            put32(a, 0x204, 0);
        }
        break;
    }
    case 3:                                                            /* 001644BC */
        if (w32(a, 0x200) & 0x1000) put8(a, 7, sub + 1);
        else put32(a, 0x204, F_0_25);
        break;
    case 4:
        FAULT(walk_back(w, a));                                        /* 001644E4 */
        break;
    case 5:
        FAULT(walk_out(w, a));                                         /* 00164530 */
        break;
    default:
        break;
    }
    return held_tail(w, a);
}

/* ---- 00163B40 ----------------------------------------------------------- */

static int state8(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    switch (b8(a, 6)) {
    case 0xA: return land_heavy(w, a);                                 /* 00163BF8 */
    case 5: return land_step(w, a, 0x6E);                              /* 00163BE8 */
    case 4: return land_step(w, a, 0x6D);                              /* 00163BD8 */
    case 3: return land_hurt(w, a);                                    /* 00163BC8 */
    case 2: return land_roll(w, a);                                    /* 00163BB8 */
    case 1: return teleport(w, a, 0x78, 0);                            /* 00163BA8 */
    default: return 0;
    }
}

/* ---- 001639E0 ----------------------------------------------------------- */

static int state7(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 6);
    if (sub == 2) return teleport(w, a, 0x78, 0);                     /* 00163B1C */
    if (sub == 0) {                                                    /* 00163A28 */
        put8(a, 6, sub + 1);
        put8(a, 7, 0);
        put32(a, 0x38, 0);
        put8(a, 0x25C, 0);
        FAULT(request(w, a, 0x72, 0, 8.0f));
    } else if (sub != 1) {
        return 0;
    }
    int landed = 0;                                                    /* 00163A50 */
    FAULT(land_check(w, a, &landed));
    int shaft = 0;
    if (b8(a, 0x23B) == 0x39 &&
        !em_ee_c_lt_bits(w32(a, 0xB4), em_ee_sub_bits(w32(a, 0x2F4), F_12)))
        shaft = 1;                                                     /* 00163A90 */
    if (landed == 0 && shaft == 0) {
        int ledge = 0;
        FAULT(w->ledge(w->context, a, w32(a, 0x2EC), &ledge));         /* 00163AA8 */
        if (ledge != 0) return 0;
    }
    int ignored = 0;
    FAULT(w->probes(w->context, a, EM_PLAYER_LAND_S1_RECORD));         /* 00163ABC */
    em_player_fall_drop(a);
    FAULT(floor_service(w, a, &ignored));
    if (b8(a, 0xA) != 0 && landed == 0) FAULT(land(w, a));             /* 00163AF0 */
    if (b8(a, 0x23A) == 0x5D) return surface5d(w, a, 0);               /* 00163B0C */
    return 0;
}

/* ---- 00162DB0 ----------------------------------------------------------- */

/* The edge test of sub-state 0: 00179450 found a floor and +258 is above
 * -(D_002488B0 - 1.8) (00162E6C / 00162ED8). */
static int edge_hit(const EmPlayerLiveActor *a, int query)
{
    if (query == 0) return 0;
    uint32_t limit = em_ee_neg_bits(em_ee_sub_bits(D_002488B0, F_1_8));
    return !em_ee_c_le_bits(w32(a, 0x258), limit);
}

/* +6 = 0xA with the tier's speed and initial drop (00162F10 and siblings). */
static int fall_speed(EmPlayerLiveActor *a, unsigned tier_offset)
{
    if (b8(a, tier_offset) > 3) return -1;  /* beyond D_00248560's 4 rows: not modelled */
    put8(a, 6, 0xA);
    uint8_t tier = b8(a, tier_offset);
    put32(a, 0x38, kFallSpeed[tier]);
    put32(a, 0x2EC, kFallDrop[b8(a, tier_offset)]);
    return 0;
}

/* The 0017D080 exit: +6 + 1, +1F0 = 0xA, 001749A0(p, 0x83, 0, 4.0). */
static int fall_hang(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    put8(a, 6, b8(a, 6) + 1);
    put8(a, 0x1F0, 0xA);
    return request(w, a, 0x83, 0, 4.0f);
}

static int state5(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    uint8_t sub = b8(a, 6);
    switch (sub) {
    case 0: {
        int query = 0, hit = 0;
        uint32_t position[3] = { w32(a, 0xB0), w32(a, 0xB4), w32(a, 0xB8) };
        FAULT(w->floor_query(w->context, a, position, &query));        /* 00162E44 */
        if (edge_hit(a, query)) hit |= 1;
        for (unsigned i = 0; i < 3; ++i) {                             /* 00162E94 */
            uint32_t matrix[16];
            for (unsigned k = 0; k < 16; ++k) matrix[k] = w32(a, 0xD0 + 4 * k);
            FAULT(w->apply(w->context, matrix, kEdgeProbe[i], w->scratch->s38A0));
            FAULT(w->floor_query(w->context, a, w->scratch->s38A0, &query));
            if (edge_hit(a, query)) hit |= 1;
        }
        if (hit != 0) {                                                /* 00162F10 */
            FAULT(fall_speed(a, 0x25C));
        } else {
            int turned = 0;
            FAULT(w->heading(w->context, a, 0, &turned));              /* 00162F58 */
            unsigned tier = turned != 0 ? 0x23F : 0x25C;
            int result = 0;
            if (b8(a, tier) == 3) {
                if (turned != 0) {
                    FAULT(w->test_001755B0(w->context, a, &result));   /* 00162F78 */
                    if (result == 0) {
                        FAULT(fall_speed(a, tier));
                        goto tail;
                    }
                } else {
                    FAULT(fall_speed(a, tier));                        /* 00163068 */
                    goto tail;
                }
            }
            FAULT(w->test_0017D080(w->context, a, &result));           /* 00162FD0 / 001630B0 */
            if (result != 0) FAULT(fall_hang(w, a));
            else FAULT(fall_speed(a, tier));
        }
    tail:
        put32(a, 0x2F4, w32(a, 0xB4));                                 /* 00163138 */
        put8(a, 0x25F, 2);
        return 0;
    }
    case 1:                                                            /* 00163148 */
        if (!(w32(a, 0x200) & 0x8000)) put8(a, 6, sub + 1);
        return 0;
    case 2:                                                            /* 00163164 */
        if (em_ee_c_le_bits(w32(a, 0x3C), F_15)) {
            put8(a, 6, sub + 1);
            put32(a, 0xB4, em_ee_sub_bits(w32(a, 0x294), F_0_8));
        }
        return 0;
    case 3:                                                            /* 001631AC */
        if (em_ee_c_le_bits(w32(a, 0x3C), F_12)) {
            put8(a, 6, sub + 1);
            FAULT(sound(w, a, 0xFF));
        }
        return 0;
    case 4: {                                                          /* 001631F0 */
        if (!(w32(a, 0x200) & 0x1000)) return 0;
        put32(a, 0xB0, w32(a, 0x290));
        put32(a, 0xB8, w32(a, 0x298));
        put32(a, 0xB4, em_ee_sub_bits(w32(a, 0x294), F_20_5));
        int clip = 0;
        FAULT(w->pose_clip(w->context, a, &clip));
        FAULT(request(w, a, clip, 0, 0.0f));
        uint32_t yaw = 0;
        FAULT(w->wrap(w->context, em_ee_add_bits(F_PI, w32(a, 0xC4)), &yaw));   /* 00163254 */
        put32(a, 0xC4, yaw);
        uint32_t matrix[16], position[3], rotation[3], scale[3];
        for (unsigned i = 0; i < 3; ++i) {
            position[i] = w32(a, 0xB0 + 4 * i);
            rotation[i] = w32(a, 0xC0 + 4 * i);
            scale[i] = w32(a, 0x60 + 4 * i);
        }
        for (unsigned k = 0; k < 16; ++k) matrix[k] = w32(a, 0xD0 + 4 * k);
        FAULT(w->trs(w->context, matrix, position, rotation, scale));  /* 00163268 */
        for (unsigned k = 0; k < 16; ++k) put32(a, 0xD0 + 4 * k, matrix[k]);
        int hang = 0;
        FAULT(w->test_0017F320(w->context, a, &hang));
        if (hang != 0) {
            put8(a, 6, 5);                                             /* 00163288 */
        } else {
            put8(a, 5, 9);                                             /* 00163290 */
            put8(a, 6, 0);
            put8(a, 0x1F0, 0x10);
            put8(a, 0xD, 0);
        }
        return 0;
    }
    case 5:                                                            /* 001632A8 */
        put32(a, 0x2F4, w32(a, 0xB4));
        put8(a, 5, 7);
        put8(a, 6, 0);
        put8(a, 0x1F0, 0xD);
        put32(a, 0x2EC, F_MINUS_0_2);
        return 0;
    case 0xA: {                                                        /* 001632D8 */
        put8(a, 6, sub + 1);
        put8(a, 7, 0);
        put32(a, 0x2E0, em_ee_div_bits(w32(a, 0x38), F_60));
        int32_t frames = 0;
        FAULT(w->clip_frames(w->context, w32(a, 0x40), 0x73, &frames));
        w->scratch->s3A20 = em_ee_cvt_s_w_bits((uint32_t)frames);      /* 00163320 */
        FAULT(w->arbiter(w->context, a, 0x73, 8.0f,
                         em_ee_float(em_ee_sub_bits(w->scratch->s3A20, F_10))));
        FAULT(w->land_sound(w->context, a, 0));
    }
        /* fall through */
    case 0xB: {                                                        /* 00163348 */
        uint32_t lean = 0;
        FAULT(w->approach(w->context, F_ZERO, w32(a, 0xC0), F_ANGLE, &lean));
        put32(a, 0xC0, lean);
        int landed = 0;
        FAULT(land_check(w, a, &landed));
        uint32_t speed = w32(a, 0x38), rate = w32(a, 0x2E0);           /* 0016336C */
        if (!em_ee_c_le_bits(speed, rate)) {
            put32(a, 0x38, em_ee_sub_bits(speed, rate));
            FAULT(w->translate(w->context, a, 1));                     /* 00163390 */
        } else {
            put32(a, 0x38, 0);
            FAULT(w->probes(w->context, a, EM_PLAYER_LAND_S1_ZERO));   /* 001633A4 */
        }
        em_player_fall_drop(a);
        int ignored = 0;
        FAULT(floor_service(w, a, &ignored));                          /* 001633BC */
        if (b8(a, 0xA) != 0) {
            if (landed == 0) FAULT(land(w, a));                        /* 001633D8 */
        } else if ((w32(a, 0x200) & 0x1000) && em_ee_c_le_bits(w32(a, 0x38), F_ZERO) &&
                   landed == 0) {
            put8(a, 5, 7);                                             /* 00163420 */
            put8(a, 6, 0);
            put8(a, 0x1F0, 0xD);
            FAULT(request(w, a, 0x72, 0, 8.0f));
        }
        if (b8(a, 0x23A) == 0x5D) return surface5d(w, a, 0);           /* 0016345C */
        return 0;
    }
    case 0x63:
        return teleport(w, a, 0x78, 0);                                /* 00163470 */
    default:
        return 0;
    }
}

/* ---- entry points ------------------------------------------------------- */

#define ENTRY(w, a) do { if (!em_player_fall_workers_bound(w) || !(a)) return -1; } while (0)

int em_player_fall_land(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land(w, a);
}

int em_player_fall_land_check(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int *result)
{
    ENTRY(w, a);
    if (!result) return -1;
    return land_check(w, a, result);
}

int em_player_fall_surface5d(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int arg)
{
    ENTRY(w, a);
    return surface5d(w, a, arg);
}

int em_player_fall_teleport(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a, int frames,
                            int hold)
{
    ENTRY(w, a);
    return teleport(w, a, frames, hold);
}

int em_player_fall_00163C10(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land_step(w, a, 0x6E);
}

int em_player_fall_00163D50(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land_step(w, a, 0x6D);
}

int em_player_fall_00163E90(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land_hurt(w, a);
}

int em_player_fall_00164220(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land_roll(w, a);
}

int em_player_fall_001643B0(const EmPlayerLandWorkers *w, EmPlayerLiveActor *a)
{
    ENTRY(w, a);
    return land_heavy(w, a);
}

int em_player_fall_state5(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerLandWorkers *w = context;
    ENTRY(w, a);
    return state5(w, a);
}

int em_player_fall_state7(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerLandWorkers *w = context;
    ENTRY(w, a);
    return state7(w, a);
}

int em_player_fall_state8(void *context, EmPlayerLiveActor *a)
{
    const EmPlayerLandWorkers *w = context;
    ENTRY(w, a);
    return state8(w, a);
}
