/* em_player_heading_record.c - 00174AC0 over the raw player record
 * (see em_player_heading_record.h, docs/PLAYER_HEADING_RECORD.md).
 *
 * Read from the decomp's byte-matched C (src/func_00174AC0.c) and checked
 * against the original instructions for evaluation order and operand order.
 * Every address in a comment is the original instruction translated there.
 * Float operations and compares go through em_ee_float.h on bit patterns. */
#include "game/em_player_heading_record.h"

#include "game/em_ee_float.h"
#include "game/em_player_stage_workers.h"
#include "game/em_script_host_workers.h"

#define F_ZERO       UINT32_C(0x00000000)
#define F_HALF       UINT32_C(0x3F000000)   /* 0.5 (00174CA4) */
#define F_256        UINT32_C(0x43800000)   /* 256.0 (00174BB4 / 00174C04) */
#define F_PI         UINT32_C(0x40490FDB)   /* pi (00174BC4 / 00174C14 / 00174C44) */
#define F_REVERSAL   UINT32_C(0x4016CBE4)   /* 3pi/4 (00174CDC) */
#define F_REVERSAL_N UINT32_C(0xC016CBE4)   /* -3pi/4 (00174D14) */
#define F_WIDE       UINT32_C(0x3F71463B)   /* 0.9424779 = 0.3pi (00174E30) */
#define F_SLOW       UINT32_C(0x3DCCCCCD)   /* 0.1 */
#define F_MID        UINT32_C(0x3E99999A)   /* 0.3 */
#define F_FAST       UINT32_C(0x3F4CCCCD)   /* 0.8 */
/* Turn steps (radians per call). */
#define STEP_4DEG    UINT32_C(0x3D8EFA35)   /* 0.06981317 */
#define STEP_6DEG    UINT32_C(0x3DD67750)   /* 0.10471976 */
#define STEP_7DEG    UINT32_C(0x3DFA35DE)   /* 0.122173056 */
#define STEP_8DEG    UINT32_C(0x3E0EFA35)   /* 0.13962634 */
#define STEP_9DEG    UINT32_C(0x3E20D97C)   /* 0.15707964 */
#define STEP_10P5DEG UINT32_C(0x3E3BA866)   /* 0.18325958 */
#define STEP_22P5DEG UINT32_C(0x3EC90FDB)   /* 0.39269909 */

static int fail(EmPlayerHeadingRecord *h, uint32_t address)
{
    if (h && h->fault_address == 0) h->fault_address = address;
    return -1;
}

/* 001B1470 (em_player_001B1470) over the bounded domain. */
static int wrap(EmPlayerHeadingRecord *h, uint32_t x, uint32_t *out)
{
    if ((x & UINT32_C(0x7FFFFFFF)) >= EM_SCRIPT_HOST_WRAP_LIMIT) return fail(h, 0x001B1470u);
    *out = em_player_001B1470(x);
    return 0;
}

/* 001B12B0(target, current, step) (em_script_host_001B12B0). Its only
 * fault with a result pointer is its own 001B1470 domain bound. */
static int approach(EmPlayerHeadingRecord *h, uint32_t target, uint32_t current, uint32_t step,
                    uint32_t *out)
{
    if (em_script_host_001B12B0(NULL, target, current, step, out) < 0)
        return fail(h, 0x001B1470u);
    return 0;
}

static int cosine(EmPlayerHeadingRecord *h, uint32_t x, uint32_t *out)
{
    float r = 0.0f;
    uint32_t fault = 0;
    if (em_sdk_math_original_0011DE90(h->world.sdk_tables, em_ee_float(x), &r, &fault) < 0)
        return fail(h, fault ? fault : 0x0011DE90u);
    *out = em_ee_bits(r);
    return 0;
}

static int arctangent2(EmPlayerHeadingRecord *h, uint32_t y, uint32_t x, uint32_t *out)
{
    const EmPlayerHeadingRecordWorld *w = &h->world;
    float r = 0.0f;
    uint32_t fault = 0;
    if (em_sdk_math_original_0011E620(w->sdk_tables, w->sdk_world, w->sdk_workers, em_ee_float(y),
                                      em_ee_float(x), &r, &fault) < 0)
        return fail(h, fault ? fault : 0x0011E620u);
    *out = em_ee_bits(r);
    return 0;
}

/* pi * (byte / 256.0): the unsigned byte converted to float (its value is
 * below 2^31, so the conversion is the plain word conversion), divided, then
 * multiplied with pi as the left operand. */
static uint32_t stick_angle(uint8_t byte)
{
    const uint32_t value = em_ee_cvt_s_w_bits((uint32_t)byte);
    return em_ee_mul_bits(F_PI, em_ee_div_bits(value, F_256));
}

int em_player_heading_record_00174AC0(EmPlayerHeadingRecord *h, EmPlayerLiveActor *a,
                                      int32_t arg, int32_t *result)
{
    if (!h) return -1;
    const EmPlayerHeadingRecordWorld *w = &h->world;
    if (!a || !result || !w->spad3B8D || !w->d810E57 || !w->d810E64 || !w->d810E65 ||
        !w->d8106A0 || !w->spad3A20 || !w->sdk_tables || !w->sdk_world)
        return fail(h, 0x00174AC0u);

    if (*w->spad3B8D != 0) {                                        /* 00174AD8 / 00174AE0 */
        em_live_set_u8(a, 0x23F, 0);                                /* 00174AE8 */
        em_live_set_u32(a, 0x240, 0);                               /* 00174AEC */
        em_live_set_u32(a, 0x24C, 0);                               /* 00174AF0 */
        *result = 0;                                                /* 00174AF8 */
        return 0;
    }
    em_live_set_u8(a, 0x23F, *w->d810E57);                          /* 00174B00 / 00174B08 */
    switch (em_live_u8(a, 0x23F)) {                                 /* 00174B0C */
    case 3: em_live_set_u32(a, 0x240, F_FAST); break;               /* 00174B78 */
    case 2: em_live_set_u32(a, 0x240, F_MID); break;                /* 00174B6C */
    case 1: em_live_set_u32(a, 0x240, F_SLOW); break;               /* 00174B5C */
    case 0:
        em_live_set_u32(a, 0x240, 0);                               /* 00174B40 */
        em_live_set_u32(a, 0x24C, 0);                               /* 00174B44 */
        *result = 0;                                                /* 00174B4C */
        return 0;
    default: break;                                                 /* 00174B38: +240 kept */
    }

    /* The stick angles: Y first (kept for the second cosine), then X. */
    const uint32_t y_angle = stick_angle(*w->d810E65);              /* 00174B80..00174BD8 */
    const uint32_t x_angle = stick_angle(*w->d810E64);              /* 00174BCC..00174C24 */
    uint32_t c;
    if (cosine(h, x_angle, &c) < 0) return -1;                      /* 00174C20 */
    em_live_set_u32(a, 0x244, c);                                   /* 00174C28 */
    if (cosine(h, y_angle, &c) < 0) return -1;                      /* 00174C2C */
    em_live_set_u32(a, 0x248, c);                                   /* 00174C34 */
    uint32_t stick;
    if (arctangent2(h, em_ee_neg_bits(c), em_live_u32(a, 0x244), &stick) < 0) /* 00174C38..00174C40 */
        return -1;
    em_live_set_u32(a, 0x24C, stick);                               /* 00174C4C */
    uint32_t ang;
    if (wrap(h, em_ee_add_bits(em_ee_add_bits(F_PI, stick), *w->d8106A0), &ang) < 0) /* 00174C5C..00174C64 */
        return -1;

    if (em_live_u8(a, 0x5) == 1) {                                  /* 00174C68 / 00174C70 */
        const unsigned mode = em_live_u8(a, 0x1F0);                 /* 00174C78 */
        if (mode == 7 || mode == 6) {                               /* 00174C80 / 00174C8C */
            arg = 0;                                                /* 00174C84 / 00174C94 */
        } else if (!em_ee_c_le_bits(em_live_u32(a, 0x38), F_HALF) && /* 00174CB0 / 00174CB8 */
                   em_live_u8(a, 0x23F) >= 2) {                     /* 00174CC0..00174CC8 */
            uint32_t error;
            if (wrap(h, em_ee_sub_bits(ang, em_live_u32(a, 0xC4)), &error) < 0) /* 00174CD0 / 00174CD4 */
                return -1;
            *w->spad3A20 = error;                                   /* 00174CF8 */
            if (!em_ee_c_le_bits(error, F_REVERSAL)) {              /* 00174CEC / 00174CF4 */
                em_live_set_u8(a, 0x1F0, 7);                        /* 00174D00 */
                em_live_set_u8(a, 0x1F1, 4);                        /* 00174D08 */
                arg = 0;                                            /* 00174D10 */
            } else if (em_ee_c_lt_bits(error, F_REVERSAL_N)) {      /* 00174D24 / 00174D2C */
                em_live_set_u8(a, 0x1F0, 7);                        /* 00174D38 */
                em_live_set_u8(a, 0x1F1, 3);                        /* 00174D40 */
                arg = 0;                                            /* 00174D44 */
            }
        }
    }

    if (arg == 1) {                                                 /* 00174D4C */
        uint32_t step, yaw;
        if (em_ee_c_eq_bits(F_ZERO, em_live_u32(a, 0x38))) {        /* 00174D60 / 00174D68 */
            const unsigned gait = em_live_u8(a, 0x23F);             /* 00174D70 */
            step = gait == 1 ? STEP_4DEG                            /* 00174D74 */
                 : gait == 2 ? STEP_8DEG                            /* 00174DA0 */
                 : STEP_22P5DEG;
            if (approach(h, ang, em_live_u32(a, 0xC4), step, &yaw) < 0) /* 00174D8C / 00174DB8 / 00174DD8 */
                return -1;
            em_live_set_u32(a, 0xC4, yaw);                          /* 00174D98 / 00174DC4 / 00174DE0 */
            if (em_live_u8(a, 0x23F) < 2 &&                         /* 00174DE4..00174DEC */
                !em_ee_c_eq_bits(ang, em_live_u32(a, 0xC4)))        /* 00174DF8 / 00174E00 */
                em_live_set_u8(a, 0x25D, 1);                        /* 00174E10 */
        } else {
            uint32_t error;
            if (wrap(h, em_ee_sub_bits(ang, em_live_u32(a, 0xC4)), &error) < 0) /* 00174E18 / 00174E1C */
                return -1;
            *w->spad3A20 = error;                                   /* 00174E24 */
            error = em_ee_bits(em_sdk_math_original_0011DF78(em_ee_float(error))); /* 00174E28 */
            *w->spad3A20 = error;                                   /* 00174E4C */
            const uint32_t speed = em_live_u32(a, 0x38);            /* 00174E50 / 00174EF4 */
            if (!em_ee_c_le_bits(error, F_WIDE)) {                  /* 00174E40 / 00174E48 */
                step = em_ee_c_le_bits(speed, F_SLOW) ? STEP_6DEG   /* 00174E64 / 00174E6C */
                     : em_ee_c_le_bits(speed, F_MID) ? STEP_9DEG    /* 00174EA4 / 00174EAC */
                     : STEP_10P5DEG;
            } else {
                step = em_ee_c_le_bits(speed, F_SLOW) ? STEP_4DEG   /* 00174F08 / 00174F10 */
                     : em_ee_c_le_bits(speed, F_MID) ? STEP_6DEG    /* 00174F48 / 00174F50 */
                     : STEP_7DEG;
            }
            if (approach(h, ang, em_live_u32(a, 0xC4), step, &yaw) < 0) /* 00174E84 .. 00174F88 */
                return -1;
            em_live_set_u32(a, 0xC4, yaw);                          /* 00174E90 .. 00174F94 */
        }
    } else if (arg == 2) {                                          /* 00174F9C */
        em_live_set_u32(a, 0x218, ang);                             /* 00174FA4 */
    }
    *result = em_live_u8(a, 0x23F);                                 /* 00174FA8 */
    return 0;
}

int em_player_heading_record_worker_result(void *context, EmPlayerLiveActor *actor, int arg,
                                           int *result)
{
    int32_t r = 0;
    if (!result) return fail(context, 0x00174AC0u);
    if (em_player_heading_record_00174AC0(context, actor, (int32_t)arg, &r) < 0) return -1;
    *result = (int)r;
    return 0;
}

int em_player_heading_record_worker(void *context, EmPlayerLiveActor *actor, int arg)
{
    int32_t r = 0;
    return em_player_heading_record_00174AC0(context, actor, (int32_t)arg, &r);
}
