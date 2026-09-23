/* em_camera_area11_specials.c - see em_camera_area11_specials.h and
 * docs/CAMERA_AREA11_SPECIALS.md.
 *
 * Read from the original instructions (build/asm of the decomp), not from
 * the readable decompilation alone: 00195130, 00193EB0, 001936E0 and
 * 00197490 are NEARMISS C, and their C differs from the instructions in
 * these places (the doc, section 2, lists them with the addresses):
 *   - 00195130 area 8, room 3, default arm: the 0x70003A24 offset for the
 *     -20 limit is 0.5 * (t - limit) (00195584..00195598), not 0.5 * t;
 *   - 00193EB0 area 0x13: reaction 0xD is set when D_00810701 == 0
 *     (00194038), and the x >= 872 split sends z <= 900 to event 0xC with
 *     the 365 height limit and z > 900 to event 0xB (001940D8);
 *   - 00197490 codes 0x29 and 0xC return at once (0019756C, 0019754C):
 *     they do not clear +2/+3/+8; 00198440 gets (cam, player, 1);
 *   - 001936E0: 00193660 gets (cam, player) (00193918).
 * Every address in a comment is the original instruction translated there.
 * tools/test_camera_area11_specials_reference.py executes the original
 * instructions and compares every byte, word and worker call. */
#include "game/em_camera_area11_specials.h"
#include "game/em_ee_float.h"

#include <stddef.h>
#include <string.h>

/* Float constants as the instructions build them (lui/ori). */
#define F_0         UINT32_C(0x00000000)
#define F_0_25      UINT32_C(0x3E800000)
#define F_0_32      UINT32_C(0x3EA3D70A)
#define F_0_48      UINT32_C(0x3EF5C28F)
#define F_0_5       UINT32_C(0x3F000000)
#define F_0_6       UINT32_C(0x3F19999A)
#define F_0_7       UINT32_C(0x3F333333)
#define F_0_8       UINT32_C(0x3F4CCCCD)
#define F_1         UINT32_C(0x3F800000)
#define F_1_1       UINT32_C(0x3F8CCCCD)
#define F_1_2       UINT32_C(0x3F99999A)
#define F_1_5       UINT32_C(0x3FC00000)
#define F_2         UINT32_C(0x40000000)
#define F_4         UINT32_C(0x40800000)
#define F_6         UINT32_C(0x40C00000)
#define F_7         UINT32_C(0x40E00000)
#define F_8         UINT32_C(0x41000000)
#define F_9         UINT32_C(0x41100000)
#define F_10        UINT32_C(0x41200000)
#define F_11        UINT32_C(0x41300000)
#define F_11_5      UINT32_C(0x41380000)
#define F_19        UINT32_C(0x41980000)
#define F_20        UINT32_C(0x41A00000)
#define F_23        UINT32_C(0x41B80000)
#define F_25        UINT32_C(0x41C80000)
#define F_30        UINT32_C(0x41F00000)
#define F_54        UINT32_C(0x42580000)
#define F_64        UINT32_C(0x42800000)
#define F_90        UINT32_C(0x42B40000)
#define F_M10       UINT32_C(0xC1200000)
#define F_M20       UINT32_C(0xC1A00000)
#define F_M43       UINT32_C(0xC22C0000)
#define F_M46_8     UINT32_C(0xC23B3333)
#define F_507       UINT32_C(0x43FD8000)

#define FAULT_AT(s, address, expr)                                    \
    do {                                                              \
        if ((expr) < 0) {                                             \
            if (!(s)->fault_address) (s)->fault_address = (address);  \
            return -1;                                                \
        }                                                             \
    } while (0)

/* ---- raw access ------------------------------------------------------- */

static uint32_t cw(const uint8_t *cam, unsigned at) { uint32_t v; memcpy(&v, cam + at, 4); return v; }
static void cput(uint8_t *cam, unsigned at, uint32_t v) { memcpy(cam + at, &v, 4); }
static uint16_t ch(const uint8_t *cam, unsigned at) { uint16_t v; memcpy(&v, cam + at, 2); return v; }
static void chput(uint8_t *cam, unsigned at, uint16_t v) { memcpy(cam + at, &v, 2); }
static uint32_t pw(const EmPlayerLiveActor *p, unsigned at) { return em_live_u32(p, at); }
static int32_t code_of(const EmPlayerLiveActor *p) { return (int32_t)em_live_u32(p, 0x230); }

/* 00102948: the 16-byte copy (dst, src). */
static void copy16(void *dst, const void *src) { memmove(dst, src, 16); }
/* 0011DF78: fabsf, the sign bit cleared (an integer AND in the original). */
static uint32_t fabs_bits(uint32_t x) { return x & UINT32_C(0x7FFFFFFF); }

static int world_ok(const EmCamSpecials *s)
{
    const EmCamSpecialsWorld *w = &s->world;
    return w->d8101E0 && w->d8105D0 && w->d8105E0 && w->d81069C && w->d8106B8 && w->d8106F2 &&
           w->d810700 && w->d810701 && w->d810702 && w->d81078B && w->d810803 && w->d810E74 &&
           w->spad;
}

/* The squared XZ distance of (x, z) from (cx, cz) as the instructions form
 * it: MULA.S of the x difference, then MADD.S of the z difference. */
static uint32_t dist2(uint32_t x, uint32_t cx, uint32_t z, uint32_t cz)
{
    uint32_t dx = em_ee_sub_bits(x, cx);
    uint32_t acc = em_ee_mula_bits(dx, dx);
    uint32_t dz = em_ee_sub_bits(z, cz);
    return em_ee_madd_bits(acc, dz, dz);
}

/* ---- 00191210 ----------------------------------------------------------- */

static void leaf_00191210(const EmCamSpecialsWorld *w)
{
    if (*w->d810700 != 0x10 || *w->d810702 != 0 || *w->d81078B == 0xFF) return;  /* 0019121C.. */
    if (em_ee_c_lt_bits(cw(w->d8101E0, 0x18), F_507))                            /* 00191260 */
        cput(w->d8101E0, 0x18, F_507);
    if (em_ee_c_lt_bits(w->d8105D0[2], F_507))                                   /* 00191290 */
        w->d8105D0[2] = F_507;
}

int em_cam_specials_00191210(EmCamSpecials *s)
{
    if (!s || !world_ok(s)) {
        if (s && !s->fault_address) s->fault_address = 0x00191210u;
        return -1;
    }
    leaf_00191210(&s->world);
    return 0;
}

/* ---- readiness ---------------------------------------------------------- */

#define NEED(ptr, address) do { if (!(ptr)) return (address); } while (0)

static uint32_t missing_00193EB0(const EmCamSpecials *s)
{
    NEED(s->w.w_00191000, 0x00191000u);
    uint8_t area = *s->world.d810700;
    if (area == 0x13 || area == 0xD) NEED(s->w.w_001B0C60, 0x001B0C60u);
    return 0;
}

/* The workers 00195130 can reach from camera state `st` in area `area`. */
static uint32_t missing_00195130_area(const EmCamSpecials *s, uint8_t area)
{
    const EmCamSpecialsWorkers *k = &s->w;
    switch (area) {
    case 0:
        NEED(k->w_00194DB0, 0x00194DB0u);
        break;
    case 6:
        NEED(k->w_001944B0, 0x001944B0u);
        NEED(k->w_00194D10, 0x00194D10u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        NEED(k->w_0018C6A0, 0x0018C6A0u);
        break;
    case 8:
        NEED(k->w_001944B0, 0x001944B0u);
        NEED(k->w_00191D40, 0x00191D40u);
        NEED(k->w_00192010, 0x00192010u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        break;
    case 0xB:
        NEED(k->w_0022FCA0, 0x0022FCA0u);
        NEED(k->w_00192010, 0x00192010u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        NEED(k->w_0018C6A0, 0x0018C6A0u);
        break;
    case 0xD:
        NEED(k->w_00823FE0, 0x00823FE0u);
        NEED(k->w_00194D10, 0x00194D10u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        NEED(k->w_0018C6A0, 0x0018C6A0u);
        break;
    case 0xE:
        NEED(k->w_00230230, 0x00230230u);
        break;
    case 0x13:
        NEED(k->w_001944B0, 0x001944B0u);
        NEED(k->w_00192010, 0x00192010u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        NEED(k->w_0018C6A0, 0x0018C6A0u);
        break;
    default:
        break;
    }
    return 0;
}

static uint32_t missing_00195130(const EmCamSpecials *s, const uint8_t *cam)
{
    const EmCamSpecialsWorkers *k = &s->w;
    if (!world_ok(s)) return 0x00195130u;
    switch (cam[1]) {
    case 0:
    case 1:
        NEED(k->w_001916C0, 0x001916C0u);
        NEED(k->w_001921D0, 0x001921D0u);
        {
            uint32_t m = missing_00195130_area(s, *s->world.d810700);
            if (m) return m;
        }
        break;
    case 2:
        NEED(k->w_00193D90, 0x00193D90u);
        NEED(k->w_0018D7B0, 0x0018D7B0u);
        break;
    case 3:
        NEED(k->w_001921D0, 0x001921D0u);
        break;
    case 4:
        NEED(k->w_001916C0, 0x001916C0u);
        NEED(k->w_0018C6A0, 0x0018C6A0u);
        NEED(k->w_0018C4B0, 0x0018C4B0u);
        NEED(k->w_001AEDE0, 0x001AEDE0u);
        break;
    default:
        break;
    }
    return missing_00193EB0(s);
}

static uint32_t missing_001936E0(const EmCamSpecials *s)
{
    const EmCamSpecialsWorkers *k = &s->w;
    if (!world_ok(s)) return 0x001936E0u;
    NEED(k->w_00102C58, 0x00102C58u);
    NEED(k->w_001026A0, 0x001026A0u);
    NEED(k->w_0018C4B0, 0x0018C4B0u);
    NEED(k->w_0018C6A0, 0x0018C6A0u);
    NEED(k->w_0018D7B0, 0x0018D7B0u);
    NEED(k->w_00193660, 0x00193660u);
    NEED(k->w_0011E748, 0x0011E748u);
    NEED(k->w_0011E620, 0x0011E620u);
    NEED(k->w_001B1470, 0x001B1470u);
    NEED(k->w_0011E2A8, 0x0011E2A8u);
    NEED(k->w_0011DE90, 0x0011DE90u);
    NEED(k->w_001B1240, 0x001B1240u);
    return 0;
}

static uint32_t missing_00197490(const EmCamSpecials *s)
{
    const EmCamSpecialsWorkers *k = &s->w;
    if (!world_ok(s)) return 0x00197490u;
    NEED(k->w_00197870, 0x00197870u);
    NEED(k->w_00198440, 0x00198440u);
    NEED(k->w_001912B0, 0x001912B0u);
    NEED(k->w_0019A910, 0x0019A910u);
    NEED(k->w_001916C0, 0x001916C0u);
    NEED(k->w_00102C58, 0x00102C58u);
    NEED(k->w_001026A0, 0x001026A0u);
    NEED(k->w_001B1240, 0x001B1240u);
    NEED(k->w_001B0300, 0x001B0300u);
    return 0;
}

int em_cam_specials_ready_00195130(const EmCamSpecials *s, const uint8_t *cam)
{
    return s && cam && missing_00195130(s, cam) == 0;
}

int em_cam_specials_ready_00193EB0(const EmCamSpecials *s)
{
    return s && world_ok(s) && missing_00193EB0(s) == 0;
}

int em_cam_specials_ready_001936E0(const EmCamSpecials *s) { return s && missing_001936E0(s) == 0; }
int em_cam_specials_ready_00197490(const EmCamSpecials *s) { return s && missing_00197490(s) == 0; }

static int refuse(EmCamSpecials *s, uint32_t missing)
{
    if (s && !s->fault_address) s->fault_address = missing;
    return -1;
}

/* ---- 00193EB0 ----------------------------------------------------------- */

/* The region event: 001B0C60(0x13, 0, event), then reaction +6 = 7 (+1 is
 * left as it is). */
static int region_event(EmCamSpecials *s, uint8_t *cam, int32_t event)
{
    FAULT_AT(s, 0x001B0C60u, s->w.w_001B0C60(s->w.ctx, 0x13, 0, event));
    cam[6] = 7;
    return 0;
}

static int router(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player, int32_t handled)
{
    const EmCamSpecialsWorld *w = &s->world;
    int32_t code = code_of(player);                                   /* 00193EBC */
    switch (code) {
    case 8: case 9: case 7: case 6: case 0x2D: case 0x2C: {           /* 00193FFC */
        uint8_t area = *w->d810700;
        if (area == 0x16) { cam[6] = 0xC; cam[1] = 0; return 0; }
        if (area == 0x13) {
            if (*w->d810701 == 0) { cam[6] = 0xD; cam[1] = 0; return 0; }  /* 00194038 */
            if (*w->d8106B8 != 0 || w->spad->s3B8D != 0) return 0;
            if (em_ee_c_lt_bits(pw(player, 0xA0), UINT32_C(0x445A0000))) {   /* 00194080: 872 */
                if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x43B20000)))  /* 356 */
                    return region_event(s, cam, 0xD);
                return 0;
            }
            if (em_ee_c_le_bits(pw(player, 0xA8), UINT32_C(0x44610000))) {   /* 001940D8: 900 */
                if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x43B68000)))  /* 00194134: 365 */
                    return region_event(s, cam, 0xC);
                return 0;
            }
            if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x43B20000)))      /* 001940F8: 356 */
                return region_event(s, cam, 0xB);
            return 0;
        }
        if (area != 0xD) return 0;                                    /* 0019415C */
        if (*w->d8106B8 != 0 || w->spad->s3B8D != 0) return 0;
        uint8_t index = *w->d810702;
        if (index == 4 || index == 6) {                               /* 001941A4 */
            if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x431F0000)))  /* 159 */
                return region_event(s, cam, 9);
            return 0;
        }
        if (index == 5 || index == 7) {                               /* 001941F8 */
            if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x431F0000)))
                return region_event(s, cam, 0xA);
        }
        return 0;
    }
    case 1: case 0x21:                                                /* 00193FCC */
        if (handled != 2 && cam[5] == 0 && cam[6] == 0)
            FAULT_AT(s, 0x00191000u, s->w.w_00191000(s->w.ctx, cam, player));
        return 0;
    case 0x12: cam[6] = 0xB; cam[1] = 0; return 0;                    /* 00193FBC */
    case 0x28: cam[6] = 0xE; cam[1] = 0; return 0;                    /* 00193FAC */
    case 0x29: case 0xC: cam[6] = 2; cam[1] = 0; return 0;            /* 00193F9C */
    case 0xD: case 0x2A: cam[6] = 1; cam[1] = 0; return 0;            /* 00193F8C */
    case 0x10: cam[6] = 9; cam[1] = 0; return 0;                      /* 00193F7C */
    default: return 0;
    }
}

int em_cam_specials_00193EB0(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player,
                             int32_t handled)
{
    if (!s || !cam || !player) return refuse(s, 0x00193EB0u);
    if (!world_ok(s)) return refuse(s, 0x00193EB0u);
    uint32_t m = missing_00193EB0(s);
    if (m) return refuse(s, m);
    return router(s, cam, player, handled);
}

/* ---- 00195130 ----------------------------------------------------------- */

/* The fixed-eye tail most arms share: 00192010(cam, +8C + (+5C + p+B4),
 * 25, far), 0018D7B0(cam, 5). */
static int fixed_eye(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player, uint32_t far)
{
    uint32_t y = em_ee_add_bits(cw(cam, 0x8C), em_ee_add_bits(cw(cam, 0x5C), pw(player, 0xB4)));
    FAULT_AT(s, 0x00192010u, s->w.w_00192010(s->w.ctx, cam, y, F_25, far));
    FAULT_AT(s, 0x0018D7B0u, s->w.w_0018D7B0(s->w.ctx, cam, 5));
    return 0;
}

static int ease_eye(EmCamSpecials *s, uint8_t *cam, uint32_t rate, int32_t *result)
{
    int32_t ignored = 0;
    FAULT_AT(s, 0x0018C6A0u, s->w.w_0018C6A0(s->w.ctx, cam + 0x10, s->world.d8105D0, rate,
                                               result ? result : &ignored));
    return 0;
}

static int ease_height(EmCamSpecials *s, uint8_t *cam, uint32_t rate, int32_t *result)
{
    int32_t ignored = 0;
    FAULT_AT(s, 0x0018C4B0u, s->w.w_0018C4B0(s->w.ctx, s->world.d8105D0, cw(cam, 0x14), rate,
                                               result ? result : &ignored));
    return 0;
}

static int style5(EmCamSpecials *s, uint8_t *cam)
{
    FAULT_AT(s, 0x0018D7B0u, s->w.w_0018D7B0(s->w.ctx, cam, 5));
    return 0;
}

/* Clamp +14 into [lo, hi] mirroring it into D_008105D4 when it moves. */
static void clamp_height(EmCamSpecials *s, uint8_t *cam, uint32_t lo, uint32_t hi)
{
    uint32_t y = cw(cam, 0x14);
    if (em_ee_c_le_bits(y, hi)) {
        if (em_ee_c_lt_bits(y, lo)) { cput(cam, 0x14, lo); s->world.d8105D0[1] = lo; }
    } else {
        cput(cam, 0x14, hi);
        s->world.d8105D0[1] = hi;
    }
}

static void publish_xz(EmCamSpecials *s, const uint8_t *cam)
{
    s->world.d8105D0[0] = cw(cam, 0x10);
    s->world.d8105D0[2] = cw(cam, 0x18);
}

static int ids_6789(int32_t code) { return code == 8 || code == 9 || code == 6 || code == 7; }

/* Area 8, room 3: the room's four camera rules (0019539C..00195AFC). */
static int area8_room3(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player, int *handled)
{
    int32_t code = code_of(player);                                   /* 001953D0 */
    if (ids_6789(code)) {                                             /* 001957D0 */
        uint32_t x = pw(player, 0xB0), z = pw(player, 0xB8);
        int far_arm;
        if (em_ee_c_lt_bits(dist2(x, UINT32_C(0x4301B333), z, UINT32_C(0x43168000)), F_25)) {
            far_arm = 1;                                              /* 00195808 */
        } else if (!em_ee_c_le_bits(pw(player, 0xA8), UINT32_C(0x430E0000)) &&   /* 142 */
                   !em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x43660000))) {    /* 230 */
            far_arm = 1;
        } else {
            far_arm = 0;
        }
        if (far_arm) {                                                /* 00195858 */
            if (!em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x4359199A))) {  /* 217.1 */
                cput(cam, 0x10, UINT32_C(0x4314999A));                /* 148.6 */
                cput(cam, 0x18, UINT32_C(0x43013333));                /* 129.2 */
                clamp_height(s, cam, UINT32_C(0x4370B333), UINT32_C(0x438D3333));  /* 240.7, 282.4 */
            } else {
                cput(cam, 0x10, UINT32_C(0x4308B333));                /* 136.7 */
                cput(cam, 0x18, UINT32_C(0x43360000));                /* 182.0 */
                clamp_height(s, cam, UINT32_C(0x43123333), UINT32_C(0x435F999A));  /* 146.2, 223.6 */
            }
        } else {                                                      /* 001959D8 */
            if (!em_ee_c_lt_bits(dist2(x, UINT32_C(0x4301CCCD), z, UINT32_C(0x43208000)), F_25))
                return 0;
            clamp_height(s, cam, UINT32_C(0x43123333), UINT32_C(0x435F999A));
            cput(cam, 0x10, UINT32_C(0x4308B333));
            cput(cam, 0x18, UINT32_C(0x43360000));
        }
        publish_xz(s, cam);                                           /* 00195960 */
        if (fixed_eye(s, cam, player, F_10) < 0) return -1;
        if (ease_height(s, cam, F_0_8, NULL) < 0) return -1;
        *handled = 1;
        return 0;
    }
    if (code == 0xA) {                                                /* 00195674 */
        uint32_t z = pw(player, 0xA8);
        if (em_ee_c_le_bits(z, UINT32_C(0x43160000))) return 0;       /* 150 */
        if (em_ee_c_le_bits(pw(player, 0xA4), UINT32_C(0x43660000))) return 0;  /* 230 */
        if (em_ee_c_le_bits(z, UINT32_C(0x43280000))) {               /* 168 */
            cput(cam, 0x10, UINT32_C(0x4314999A));                    /* 148.6 */
            cput(cam, 0x18, UINT32_C(0x43013333));                    /* 129.2 */
            if (!em_ee_c_le_bits(cw(cam, 0x14), UINT32_C(0x438D3333))) {  /* 282.4 */
                cput(cam, 0x14, UINT32_C(0x438D3333));
                s->world.d8105D0[1] = UINT32_C(0x438D3333);
            }
            publish_xz(s, cam);
            uint32_t y = em_ee_add_bits(cw(cam, 0x8C),
                                        em_ee_add_bits(cw(cam, 0x5C), pw(player, 0xB4)));
            FAULT_AT(s, 0x00192010u, s->w.w_00192010(s->w.ctx, cam, y, F_25, F_10));
        } else {
            cput(cam, 0x10, UINT32_C(0x4303B333));                    /* 131.7 */
            cput(cam, 0x18, UINT32_C(0x434D3333));                    /* 205.2 */
            cput(cam, 0x14, UINT32_C(0x438F999A));                    /* 287.2 */
            s->world.d8105D0[1] = UINT32_C(0x438F999A);
            publish_xz(s, cam);
        }
        if (style5(s, cam) < 0) return -1;                            /* 0019579C */
        if (ease_height(s, cam, F_0_8, NULL) < 0) return -1;
        *handled = 1;
        return 0;
    }
    /* Every other code (00195400). */
    if (!em_ee_c_lt_bits(pw(player, 0xA4), UINT32_C(0x4359199A))) return 0;  /* 217.1 */
    if (em_ee_c_lt_bits(pw(player, 0xA0), UINT32_C(0x43114CCD)) &&           /* 145.3 */
        em_ee_c_lt_bits(pw(player, 0xA8), UINT32_C(0x43230000))) {           /* 163 */
        cput(cam, 0x10, UINT32_C(0x4303199A));                        /* 131.1 */
        cput(cam, 0x18, UINT32_C(0x43368000));                        /* 182.5 */
    } else if (em_ee_c_lt_bits(pw(player, 0xA8), UINT32_C(0x43230000))) {    /* 00195480 */
        cput(cam, 0x10, UINT32_C(0x4327CCCD));                        /* 167.8 */
        cput(cam, 0x18, UINT32_C(0x4334E666));                        /* 180.9 */
    } else if (em_ee_c_eq_bits(UINT32_C(0x4303199A), cw(cam, 0x10))) {   /* 001954B4 */
        cput(cam, 0x10, UINT32_C(0x4303199A));
        cput(cam, 0x18, UINT32_C(0x43368000));
    } else {
        cput(cam, 0x10, UINT32_C(0x4327CCCD));
        cput(cam, 0x18, UINT32_C(0x4334E666));
    }
    uint32_t limit = em_ee_c_eq_bits(F_M46_8, cw(cam, 0x64)) ? F_M20 : F_M10;  /* 00195518 */
    uint32_t t = em_ee_sub_bits(*s->world.d81069C, fabs_bits(cw(cam, 0xC)));  /* 00195550 */
    s->world.spad->s3A20 = t;
    uint32_t want;
    if (em_ee_c_lt_bits(t, limit)) {                                  /* 00195558 */
        uint32_t over = em_ee_sub_bits(t, limit);
        if (!em_ee_c_eq_bits(F_M20, limit)) {                         /* 00195574 */
            s->world.spad->s3A24 = over;
            if (em_ee_c_lt_bits(over, F_M10)) s->world.spad->s3A24 = F_M10;  /* 001955B4 */
        } else {
            s->world.spad->s3A24 = em_ee_mul_bits(F_0_5, over);        /* 00195598 */
        }
        uint32_t a = em_ee_sub_bits(cw(cam, 0x5C), s->world.spad->s3A24);  /* 001955E8 */
        a = em_ee_add_bits(pw(player, 0xA4), a);
        a = em_ee_add_bits(cw(cam, 0x8C), a);
        want = em_ee_add_bits(F_11, a);
    } else {                                                          /* 001955FC */
        uint32_t a = em_ee_add_bits(cw(cam, 0x5C), pw(player, 0xA4));
        a = em_ee_add_bits(cw(cam, 0x8C), a);
        want = em_ee_add_bits(F_11, a);
    }
    FAULT_AT(s, 0x00191D40u, s->w.w_00191D40(s->w.ctx, cam, want, F_4));  /* 00195624 */
    if (style5(s, cam) < 0) return -1;
    if (ease_height(s, cam, F_0_8, NULL) < 0) return -1;
    *handled = 2;
    publish_xz(s, cam);                                               /* 00195660 */
    return 0;
}

/* The area arm of states 0/1 (001951B4..0019612C). */
static int area_arm(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player, int *handled)
{
    const EmCamSpecialsWorld *w = &s->world;
    int32_t result = 0;
    switch (*w->d810700) {
    case 0:                                                           /* 00195230 */
        if (code_of(player) == 0x14 || code_of(player) == 0x15)
            cput(cam, 0x94, em_ee_sub_bits(F_M10, cw(cam, 0xC)));
        if (*w->d810701 == 2 && *w->d810803 == 3) {
            cput(cam, 0x94, F_0);                                     /* 0019528C */
            FAULT_AT(s, 0x00194DB0u, s->w.w_00194DB0(s->w.ctx, cam, player, 8));
            *handled = 2;
        }
        return 0;
    case 4:                                                           /* 00195298 */
        if ((int8_t)cam[0x6D] == 0) return 0;
        if (em_ee_c_le_bits(pw(player, 0xA8), UINT32_C(0x43B40000)) ||    /* 360 */
            em_ee_c_le_bits(pw(player, 0xA4), F_54))
            cput(cam, 0x98, F_11_5);
        else
            cput(cam, 0x98, F_0);
        return 0;
    case 6:                                                           /* 001952F8 */
        FAULT_AT(s, 0x001944B0u, s->w.w_001944B0(s->w.ctx, cam, player, 0, &result));
        if (result != 0) { *handled = 1; return 0; }
        FAULT_AT(s, 0x00194D10u, s->w.w_00194D10(s->w.ctx, cam, player, 2, &result));
        if (result == 0) return 0;
        if (style5(s, cam) < 0) return -1;
        cput(cam, 0x14, F_90);                                        /* 00195334 */
        if (ease_height(s, cam, F_0_7, &result) < 0) return -1;
        *handled = 1;
        if (result != 0) {
            cput(cam, 0x10, UINT32_C(0xC3B7D99A));                    /* -367.7 */
            cput(cam, 0x18, UINT32_C(0xC415B99A));                    /* -598.4 */
            if (ease_eye(s, cam, F_0_7, NULL) < 0) return -1;
        }
        return 0;
    case 8:                                                           /* 0019539C */
        if (*w->d810701 == 2) {
            FAULT_AT(s, 0x001944B0u, s->w.w_001944B0(s->w.ctx, cam, player, 8, &result));
            if (result != 0) *handled = 1;
            return 0;
        }
        if (*w->d810701 != 3) return 0;
        return area8_room3(s, cam, player, handled);
    case 0xB:                                                         /* 00195B00: AREA11 */
        if (em_ee_c_lt_bits(pw(player, 0xA4), UINT32_C(0x43390000)) &&        /* y < 185 */
            em_ee_c_lt_bits(pw(player, 0xA8), UINT32_C(0x435C0000)) &&        /* z < 220 */
            !em_ee_c_le_bits(pw(player, 0xA0), UINT32_C(0x43B38000)) &&       /* x > 359 */
            em_ee_c_lt_bits(pw(player, 0xA0), UINT32_C(0x43C56666))) {        /* x < 394.8 */
            FAULT_AT(s, 0x0022FCA0u, s->w.w_0022FCA0(s->w.ctx, cam, player, 8));   /* 00195B80 */
            cput(cam, 0x18, UINT32_C(0x435B0000));                    /* 219 */
            cput(cam, 0x14, UINT32_C(0x435C0000));                    /* 220 */
            if (style5(s, cam) < 0) return -1;
            if (ease_eye(s, cam, F_1_5, NULL) < 0) return -1;
            if (ease_height(s, cam, F_1, NULL) < 0) return -1;
            *handled = 1;
            return 0;
        }
        if (!ids_6789(code_of(player))) return 0;                     /* 00195BDC */
        if (!em_ee_c_lt_bits(dist2(pw(player, 0xA0), UINT32_C(0x43A0C000),      /* 321.5 */
                                   pw(player, 0xA8), UINT32_C(0x4358B333)), F_64))  /* 216.7 */
            return 0;
        cput(cam, 0x10, UINT32_C(0x43B74CCD));                        /* 366.6 */
        cput(cam, 0x18, UINT32_C(0x4358199A));                        /* 216.1 */
        if (fixed_eye(s, cam, player, F_20) < 0) return -1;
        if (code_of(player) == 7)                                     /* 00195CAC */
            copy16(s->world.d8105D0, cam + 0x10);
        else if (ease_eye(s, cam, F_0_8, NULL) < 0)
            return -1;
        if (ease_height(s, cam, F_0_8, NULL) < 0) return -1;
        *handled = 1;
        return 0;
    case 0xD:                                                         /* 00195D08 */
        if (*w->d810702 < 8) return 0;
        FAULT_AT(s, 0x00823FE0u, s->w.w_00823FE0(s->w.ctx, cam, &result));
        if (result != 0) {
            cput(cam, 0x10, UINT32_C(0x44485333));                    /* 801.3 */
            cput(cam, 0x14, UINT32_C(0x438D2666));                    /* 282.3 */
            cput(cam, 0x18, UINT32_C(0x44926000));                    /* 1171 */
            if (style5(s, cam) < 0) return -1;
            if (ease_eye(s, cam, F_1_2, NULL) < 0) return -1;
            if (ease_height(s, cam, F_0_7, NULL) < 0) return -1;
            *handled = 1;
            return 0;
        }
        if (em_live_u8(player, 0xF) == 0xB) {                         /* 00195D94 */
            cam[1] = 4;
            *handled = 1;
            cam[2] = 0;
            return 0;
        }
        FAULT_AT(s, 0x00194D10u, s->w.w_00194D10(s->w.ctx, cam, player, 0, &result));
        if (result != 0) {
            cput(cam, 0x10, UINT32_C(0x4451F333));                    /* 839.8 */
            cput(cam, 0x14, UINT32_C(0x43460000));                    /* 198 */
            cput(cam, 0x18, UINT32_C(0x4498299A));                    /* 1217.3 */
            if (style5(s, cam) < 0) return -1;
            if (ease_eye(s, cam, F_0_7, NULL) < 0) return -1;
            if (ease_height(s, cam, F_0_7, NULL) < 0) return -1;
            *handled = 1;
            return 0;
        }
        if (em_ee_c_lt_bits(pw(player, 0xA0), UINT32_C(0x4425C000)) &&   /* 663 */
            em_ee_c_lt_bits(pw(player, 0xA8), UINT32_C(0x4438A000))) {   /* 738.5 */
            cput(cam, 0x10, UINT32_C(0x4422B99A));                    /* 650.9 */
            cput(cam, 0x14, UINT32_C(0x433B999A));                    /* 187.6 */
            cput(cam, 0x18, UINT32_C(0x44445333));                    /* 785.3 */
            if (style5(s, cam) < 0) return -1;
            if (ease_eye(s, cam, F_1, NULL) < 0) return -1;
            if (ease_height(s, cam, F_1, NULL) < 0) return -1;
            *handled = 1;
        }
        return 0;
    case 0xE:                                                         /* 00195EE0 */
        FAULT_AT(s, 0x00230230u, s->w.w_00230230(s->w.ctx, cam, player, &result));
        if (result != 0) *handled = 1;
        return 0;
    case 0xF:                                                         /* 00195EF8 */
        if (*w->d810701 == 1 && (code_of(player) == 0x14 || code_of(player) == 0x15))
            cput(cam, 0x94, em_ee_sub_bits(F_M10, cw(cam, 0xC)));
        return 0;
    case 0x11: {                                                      /* 00195F3C */
        uint32_t dx = em_ee_sub_bits(UINT32_C(0x43AA0000), pw(player, 0xA0));  /* 340 */
        uint32_t dz = em_ee_sub_bits(UINT32_C(0x43870000), pw(player, 0xA8));  /* 270 */
        uint32_t d = em_ee_madd_bits(em_ee_mula_bits(dx, dx), dz, dz);
        if (!em_ee_c_le_bits(UINT32_C(0x47242900), d) &&              /* 42025 */
            em_ee_c_lt_bits(UINT32_C(0x46992000), d))                 /* 19600 */
            cput(cam, 0x98, F_20);
        return 0;
    }
    case 0x13:                                                        /* 00195FA8 */
        if (*w->d810701 == 0) {
            FAULT_AT(s, 0x001944B0u,
                     s->w.w_001944B0(s->w.ctx, cam, player, (int32_t)*w->d8106F2, &result));
            if (result != 0) { *handled = 1; return 0; }
            if (code_of(player) == 0x14 || code_of(player) == 0x15)
                cput(cam, 0x94, em_ee_sub_bits(F_M43, cw(cam, 0xC)));
            return 0;
        }
        FAULT_AT(s, 0x001944B0u, s->w.w_001944B0(s->w.ctx, cam, player, 7, &result));
        if (result != 0) { *handled = 1; return 0; }
        if (!ids_6789(code_of(player))) return 0;
        if (!em_ee_c_lt_bits(dist2(pw(player, 0xA0), UINT32_C(0x445F0666),        /* 892.1 */
                                   pw(player, 0xA8), UINT32_C(0x44686000)), F_64))  /* 929.5 */
            return 0;
        cput(cam, 0x10, UINT32_C(0x445AB333));                        /* 874.8 */
        cput(cam, 0x18, UINT32_C(0x445DC666));                        /* 887.1 */
        if (fixed_eye(s, cam, player, F_20) < 0) return -1;
        if (ease_eye(s, cam, F_0_8, NULL) < 0) return -1;
        if (ease_height(s, cam, F_0_8, NULL) < 0) return -1;
        *handled = 1;
        return 0;
    default:
        return 0;
    }
}

/* State 4: the area-0xD pan (00196178..00196364). */
static int pan_state4(EmCamSpecials *s, uint8_t *cam)
{
    int32_t a = 0, b = 0;
    switch (cam[2]) {
    case 0:
        chput(cam, 8, 0x78);                                          /* 001961C0 */
        cam[2] = (uint8_t)(cam[2] + 1);
        /* fall through */
    case 1: {
        uint16_t left = (uint16_t)(ch(cam, 8) - 1);                   /* 001961D0 */
        chput(cam, 8, left);
        if (left == 0) cam[2] = (uint8_t)(cam[2] + 1);
        cput(cam, 0x10, UINT32_C(0x44275333));                        /* 669.3 */
        cput(cam, 0x14, UINT32_C(0x43364CCD));                        /* 182.3 */
        cput(cam, 0x18, UINT32_C(0x44875000));                        /* 1082.5 */
        if (ease_eye(s, cam, F_1_1, NULL) < 0) return -1;
        return ease_height(s, cam, F_0_6, NULL);
    }
    case 2:
        cput(cam, 0x10, UINT32_C(0x443F6666));                        /* 765.6 */
        cput(cam, 0x14, UINT32_C(0x438B8CCD));                        /* 279.1 */
        cput(cam, 0x18, UINT32_C(0x4489ECCD));                        /* 1103.4 */
        if (ease_eye(s, cam, F_0_6, &a) < 0) return -1;
        if (ease_height(s, cam, F_0_48, &b) < 0) return -1;
        if ((a | b) == 7) {                                           /* 001962C0 */
            chput(cam, 8, 0x78);
            cam[2] = (uint8_t)(cam[2] + 1);
        }
        return 0;
    case 3: {
        uint16_t left = (uint16_t)(ch(cam, 8) - 1);                   /* 001962DC */
        chput(cam, 8, left);
        if (left == 0) {
            cam[2] = (uint8_t)(cam[2] + 1);
            FAULT_AT(s, 0x001AEDE0u, s->w.w_001AEDE0(s->w.ctx, 4, 0));
        }
    }
        /* fall through */
    case 4:
        cput(cam, 0x10, UINT32_C(0x44416666));                        /* 773.6 */
        cput(cam, 0x14, UINT32_C(0x43C1A666));                        /* 387.3 */
        cput(cam, 0x18, UINT32_C(0x448E1666));                        /* 1136.7 */
        if (ease_eye(s, cam, F_0_6, NULL) < 0) return -1;
        return ease_height(s, cam, F_0_32, NULL);
    default:
        return 0;
    }
}

int em_cam_specials_00195130(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player)
{
    if (!s || !cam || !player) return refuse(s, 0x00195130u);
    uint32_t m = missing_00195130(s, cam);
    if (m) return refuse(s, m);
    int handled = 0;
    uint8_t st = cam[1];
    switch (st) {
    case 0:
        cam[1] = (uint8_t)(st + 1);                                   /* 00195194 */
        cam[2] = 0;
        cam[3] = 0;
        chput(cam, 8, 0);
        /* fall through */
    case 1: {
        FAULT_AT(s, 0x001916C0u, s->w.w_001916C0(s->w.ctx, cam, player, 0));   /* 001951AC */
        /* 001951B8 reads the area after 001916C0: a worker that changed it
         * to an area whose workers are unbound is a fault. */
        m = missing_00195130_area(s, *s->world.d810700);
        if (m) return refuse(s, m);
        if (area_arm(s, cam, player, &handled) < 0) return -1;
        if (handled == 0)                                             /* 00196130 */
            FAULT_AT(s, 0x001921D0u, s->w.w_001921D0(s->w.ctx, cam, player, 0));
        break;
    }
    case 2:                                                           /* 0019614C */
        FAULT_AT(s, 0x00193D90u, s->w.w_00193D90(s->w.ctx, cam, player, 1));
        FAULT_AT(s, 0x0018D7B0u, s->w.w_0018D7B0(s->w.ctx, cam, 0));
        break;
    case 3:                                                           /* 00196168 */
        FAULT_AT(s, 0x001921D0u, s->w.w_001921D0(s->w.ctx, cam, player, 1));
        break;
    case 4:                                                           /* 00196178 */
        FAULT_AT(s, 0x001916C0u, s->w.w_001916C0(s->w.ctx, cam, player, 0));
        if (pan_state4(s, cam) < 0) return -1;
        break;
    default:
        break;
    }
    leaf_00191210(&s->world);                                         /* 00196368 */
    m = missing_00193EB0(s);
    if (m) return refuse(s, m);
    return router(s, cam, player, handled);                           /* 00196378 */
}

/* ---- 001936E0 ----------------------------------------------------------- */

/* 001028D0: out = a - b, VSUB.xyzw. */
static int vsub4(const void *a, const void *b, uint32_t out[4])
{
    uint32_t x[4], y[4];
    memcpy(x, a, 16);
    memcpy(y, b, 16);
    return em_vu_vec_bits(EM_VU_SUB, 15, -1, x, y, 0, NULL, out) == EM_EE_FLOAT_OK ? 0 : -1;
}

/* 00102738(a, a): VMUL.xyz, VADDy.x, VADDz.x; the x lane. */
static int dot3(const uint32_t v[4], uint32_t *out)
{
    uint32_t r[4];
    memcpy(r, v, 16);
    if (em_vu_vec_bits(EM_VU_MUL, 14, -1, v, r, 0, NULL, r) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_ADDBC, 8, 1, r, r, 0, NULL, r) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_ADDBC, 8, 2, r, r, 0, NULL, r) != EM_EE_FLOAT_OK) return -1;
    *out = r[0];
    return 0;
}

/* 001029C0(m): VSUB.xyzw of vf0 from itself, VADD.w of vf0 into it, then
 * three VMR32 rotations (a lane move, no arithmetic); the four rows are
 * stored last row first. */
static int identity_001029C0(uint32_t m[16])
{
    static const uint32_t vf0[4] = {F_0, F_0, F_0, F_1};
    uint32_t v4[4] = {0, 0, 0, 0}, v5[4], v6[4], v7[4];
    if (em_vu_vec_bits(EM_VU_SUB, 15, EM_VU_NO_BC, vf0, vf0, 0, NULL, v4) != EM_EE_FLOAT_OK) return -1;
    if (em_vu_vec_bits(EM_VU_ADD, 1, EM_VU_NO_BC, v4, vf0, 0, NULL, v4) != EM_EE_FLOAT_OK) return -1;
    for (unsigned k = 0; k < 4; ++k) v5[k] = v4[(k + 1) & 3];
    for (unsigned k = 0; k < 4; ++k) v6[k] = v5[(k + 1) & 3];
    for (unsigned k = 0; k < 4; ++k) v7[k] = v6[(k + 1) & 3];
    memcpy(m + 12, v4, 16);
    memcpy(m + 8, v5, 16);
    memcpy(m + 4, v6, 16);
    memcpy(m + 0, v7, 16);
    return 0;
}

/* 001029C0 then 00102C58(M, M, cam+0x30): the camera rotation into
 * 0x70003400. */
static int stage_rotation(EmCamSpecials *s, uint8_t *cam)
{
    uint32_t *m = s->world.spad->s3400;
    if (identity_001029C0(m) < 0) return refuse(s, 0x001029C0u);
    FAULT_AT(s, 0x00102C58u, s->w.w_00102C58(s->w.ctx, m, m, cam + 0x30));
    return 0;
}

static int small_target(int32_t code) { return code == 0xF || code == 4 || code == 2; }

static void clear_1236(uint8_t *cam)
{
    cam[6] = 0;
    cam[1] = 0;
    cam[2] = 0;
    cam[3] = 0;
}

static int lockon_state0(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player)
{
    EmCamSpecialsScratch *sp = s->world.spad;
    int32_t result = 0;
    chput(cam, 0xA0, 0x78);                                           /* 00193728 */
    cam[1] = (uint8_t)(cam[1] + 1);
    cam[2] = 0;
    chput(cam, 8, 0);
    copy16(cam + 0x30, sp->s3B50);                                    /* 00193748 */
    if (stage_rotation(s, cam) < 0) return -1;
    uint32_t reach, lift;
    copy16(cam + 0x20, player->bytes + 0xA0);                         /* 001937B0 / 00193824 */
    if (small_target(code_of(player))) {                              /* 00193774 */
        reach = F_9;
        cput(cam, 0x24, em_ee_add_bits(F_11, em_ee_add_bits(pw(player, 0xA4), cw(cam, 0x8C))));
        lift = F_9;
    } else {
        reach = F_19;
        cput(cam, 0x24, em_ee_add_bits(F_11, em_ee_add_bits(F_6, pw(player, 0xA4))));
        lift = F_19;
    }
    for (unsigned i = 0; i < 3; ++i) sp->s3400[12 + i] = pw(player, 0xA0 + 4 * i);   /* 001031E0 */
    sp->s3600[0] = F_0;
    sp->s3600[1] = lift;
    sp->s3600[2] = em_ee_neg_bits(cw(cam, 0x4C));
    sp->s3600[3] = F_1;
    FAULT_AT(s, 0x001026A0u, s->w.w_001026A0(s->w.ctx, cam + 0x10, sp->s3400, sp->s3600));
    FAULT_AT(s, 0x0018C4B0u, s->w.w_0018C4B0(s->w.ctx, s->world.d8105E0, cw(cam, 0x24), F_2,
                                               &result));
    FAULT_AT(s, 0x0018C6A0u, s->w.w_0018C6A0(s->w.ctx, cam + 0x20, s->world.d8105E0, F_2,
                                               &result));
    FAULT_AT(s, 0x0018D7B0u, s->w.w_0018D7B0(s->w.ctx, cam, 6));
    if (small_target(code_of(player))) {                              /* 001938E4 */
        FAULT_AT(s, 0x00193660u, s->w.w_00193660(s->w.ctx, cam, player, &result));
        if (result != 0) clear_1236(cam);
    } else {
        if (vsub4(cam + 0x10, cam + 0x20, sp->s3630) < 0) return refuse(s, 0x001028D0u);
        uint32_t d2 = em_ee_madd_bits(em_ee_mula_bits(sp->s3630[0], sp->s3630[0]),
                                      sp->s3630[2], sp->s3630[2]);   /* 00193960 */
        uint32_t d = 0;
        FAULT_AT(s, 0x0011E748u, s->w.w_0011E748(s->w.ctx, d2, &d));
        if (em_ee_c_lt_bits(d, F_7)) {                                /* 00193978 */
            uint32_t y = pw(player, 0xA4);
            uint32_t high = em_ee_add_bits(F_30, y);
            uint32_t cur = cw(cam, 0x14);
            if (em_ee_c_le_bits(cur, high)) {                         /* 0019399C */
                uint32_t low = em_ee_add_bits(F_10, em_ee_add_bits(y, reach));
                if (em_ee_c_lt_bits(cur, low)) cput(cam, 0x14, low);
            } else {
                cput(cam, 0x14, high);
            }
        }
    }
    if (em_ee_c_lt_bits(cw(cam, 0x14), cw(cam, 0x50))) cput(cam, 0x14, cw(cam, 0x50));   /* 001939E4 */
    if (!em_ee_c_le_bits(cw(cam, 0x14), cw(cam, 0x54))) cput(cam, 0x14, cw(cam, 0x54)); /* 00193A00 */
    FAULT_AT(s, 0x0018D7B0u, s->w.w_0018D7B0(s->w.ctx, cam, 6));
    leaf_00191210(&s->world);                                         /* 00193A20 */
    return 0;
}

static int lockon_state1(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player)
{
    EmCamSpecialsScratch *sp = s->world.spad;
    uint32_t *eye = s->world.d8105D0;
    int32_t result = 0;
    int done = 0;
    FAULT_AT(s, 0x0018C4B0u, s->w.w_0018C4B0(s->w.ctx, s->world.d8105E0, cw(cam, 0x24), F_2,
                                               &result));
    FAULT_AT(s, 0x0018C6A0u, s->w.w_0018C6A0(s->w.ctx, cam + 0x20, s->world.d8105E0, F_2,
                                               &result));
    FAULT_AT(s, 0x0018C6A0u, s->w.w_0018C6A0(s->w.ctx, cam + 0x10, eye, F_4, &result));
    FAULT_AT(s, 0x0018C4B0u, s->w.w_0018C4B0(s->w.ctx, eye, cw(cam, 0x14), F_4, &result));
    if (em_ee_c_lt_bits(eye[1], em_ee_add_bits(F_23, pw(player, 0xA4)))) {   /* 00193AA8 */
        uint32_t gx = em_ee_sub_bits(eye[0], pw(player, 0xA0));
        uint32_t acc = em_ee_mula_bits(gx, gx);
        uint32_t gz = em_ee_sub_bits(eye[2], pw(player, 0xA8));
        uint32_t d = 0;
        FAULT_AT(s, 0x0011E748u, s->w.w_0011E748(s->w.ctx, em_ee_madd_bits(acc, gz, gz), &d));
        sp->s3A20 = d;                                                /* 00193AFC */
        if (em_ee_c_lt_bits(d, F_8)) {
            uint32_t a = 0, wrapped = 0, sine = 0, cosine = 0;
            FAULT_AT(s, 0x0011E620u, s->w.w_0011E620(s->w.ctx, gx, gz, &a));
            FAULT_AT(s, 0x001B1470u, s->w.w_001B1470(s->w.ctx, a, &wrapped));
            sp->s3A24 = wrapped;
            FAULT_AT(s, 0x0011E2A8u, s->w.w_0011E2A8(s->w.ctx, wrapped, &sine));
            eye[0] = em_ee_add_bits(pw(player, 0xA0), em_ee_mul_bits(F_8, sine));  /* 00193B48 */
            FAULT_AT(s, 0x0011DE90u, s->w.w_0011DE90(s->w.ctx, sp->s3A24, &cosine));
            eye[2] = em_ee_add_bits(pw(player, 0xA8), em_ee_mul_bits(F_8, cosine));
        }
    }
    chput(cam, 8, (uint16_t)(ch(cam, 8) + 1));                        /* 00193B7C */
    if (vsub4(cam + 0x10, eye, sp->s38A0) < 0) return refuse(s, 0x001028D0u);
    uint32_t dot = 0, d = 0;
    if (dot3(sp->s38A0, &dot) < 0) return refuse(s, 0x00102738u);
    FAULT_AT(s, 0x0011E748u, s->w.w_0011E748(s->w.ctx, dot, &d));
    sp->s3A20 = d;                                                    /* 00193BC0 */
    if (em_ee_c_lt_bits(d, F_0_25)) done = 1;
    if (!em_ee_c_le_bits(pw(player, 0x38), F_0))                      /* 00193BD4 */
        chput(cam, 8, (uint16_t)(ch(cam, 8) + 0xA));
    if (done || !((int16_t)ch(cam, 8) < 0x51)) clear_1236(cam);       /* 00193BF0 */
    return 0;
}

int em_cam_specials_001936E0(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player)
{
    if (!s || !cam || !player) return refuse(s, 0x001936E0u);
    uint32_t m = missing_001936E0(s);
    if (m) return refuse(s, m);
    if (cam[1] == 1) {
        if (lockon_state1(s, cam, player) < 0) return -1;
    } else if (cam[1] == 0) {
        if (lockon_state0(s, cam, player) < 0) return -1;
    }
    leaf_00191210(&s->world);                                         /* 00193C18 */
    switch (code_of(player)) {
    case 0x21: case 0xF: case 2: case 1:
        break;
    case 0x29: case 0xC:
        cam[6] = 2;
        cam[1] = 0;
        break;
    case 0xD: case 0x2A:
        cam[6] = 1;
        cam[1] = 0;
        break;
    default:
        clear_1236(cam);
        break;
    }
    if (cam[6] == 3 && (*s->world.d810E74 & s->world.spad->s3B80) != 0) {   /* 00193CBC */
        cam[1] = 0;
        cput(cam, 0x48, pw(player, 0xC4));
        uint32_t rate = fabs_bits(*s->world.d81069C);
        cput(cam, 0x4C, rate);
        if (em_ee_c_lt_bits(rate, F_7)) {                             /* 00193D0C */
            cput(cam, 0x4C, F_7);
        } else if (!em_ee_c_le_bits(cw(cam, 0x4C), fabs_bits(cw(cam, 0x64)))) {
            cput(cam, 0x4C, fabs_bits(cw(cam, 0x64)));
        }
        uint32_t yaw = 0;
        FAULT_AT(s, 0x001B1240u, s->w.w_001B1240(s->w.ctx, s->world.d8105D0, s->world.d8105E0[0],
                                                   s->world.d8105E0[2], &yaw));
        cput(cam, 0x44, yaw);
    }
    return 0;
}

/* ---- 00197490 ----------------------------------------------------------- */

int em_cam_specials_00197490(EmCamSpecials *s, uint8_t *cam, EmPlayerLiveActor *player,
                             int32_t a2)
{
    if (!s || !cam || !player) return refuse(s, 0x00197490u);
    uint32_t m = missing_00197490(s);
    if (m) return refuse(s, m);
    EmCamSpecialsScratch *sp = s->world.spad;
    int32_t code = code_of(player);                                   /* 001974A4 */
    if (code != 0x18 && code != 0x17 && code != 5) {
        switch (code) {
        case 0x29:                                                    /* 00197558 */
            cam[6] = 2;
            cam[1] = 2;
            FAULT_AT(s, 0x00198440u, s->w.w_00198440(s->w.ctx, cam, player, 1));
            return 0;
        case 0xC:                                                     /* 00197534 */
            cam[6] = 2;
            cam[1] = 2;
            FAULT_AT(s, 0x00198440u, s->w.w_00198440(s->w.ctx, cam, player, 1));
            FAULT_AT(s, 0x001912B0u, s->w.w_001912B0(s->w.ctx, player));
            return 0;
        case 0xD:
        case 0x2A:                                                    /* 00197510 */
            cam[6] = 1;
            cam[1] = 2;
            FAULT_AT(s, 0x00197870u, s->w.w_00197870(s->w.ctx, cam, player, 1));
            return 0;
        default:
            break;
        }
        if (cam[0x8B] == 0) copy16(player->bytes + 0xA0, sp->s3040);   /* 00197574 */
    }
    if (a2 != 0) {                                                    /* 0019769C */
        int32_t hit = 0;
        FAULT_AT(s, 0x0019A910u, s->w.w_0019A910(s->w.ctx, cam + 0x10, cam + 0x20, 6, &hit));
        if (hit != 0) copy16(cam + 0x20, sp->s31B0);
        FAULT_AT(s, 0x001916C0u, s->w.w_001916C0(s->w.ctx, cam, player, 2));
        chput(cam, 0xA0, 0x50);
        cam[6] = 0;
    } else if (cam[5] != 0) {
        cam[6] = 0;                                                   /* 001976D8 */
    } else {
        copy16(cam + 0x30, sp->s3B50);                                /* 001975A4 */
        copy16(cam + 0x20, player->bytes + 0xA0);
        cput(cam, 0x24, em_ee_add_bits(pw(player, 0xB4), cw(cam, 0x8C)));
        if (stage_rotation(s, cam) < 0) return -1;
        sp->s3600[0] = F_0;
        sp->s3600[1] = F_0;
        sp->s3600[2] = F_M10;
        sp->s3600[3] = F_1;
        FAULT_AT(s, 0x001026A0u, s->w.w_001026A0(s->w.ctx, cam + 0x10, sp->s3400, sp->s3600));
        cput(cam, 0x10, em_ee_add_bits(cw(cam, 0x10), cw(cam, 0x20)));   /* 00197638 */
        cput(cam, 0x14, em_ee_add_bits(cw(cam, 0x14), em_ee_add_bits(cw(cam, 0x24), cw(cam, 0x5C))));
        cput(cam, 0x18, em_ee_add_bits(cw(cam, 0x18), cw(cam, 0x28)));
        copy16(s->world.d8105E0, cam + 0x20);
        copy16(s->world.d8105D0, cam + 0x10);
        leaf_00191210(&s->world);                                     /* 0019768C */
        cam[6] = 0;
    }
    if (cam[5] == 0) {                                                /* 001976DC */
        cam[1] = 0;
        uint32_t yaw = 0;
        FAULT_AT(s, 0x001B1240u, s->w.w_001B1240(s->w.ctx, s->world.d8105D0, s->world.d8105E0[0],
                                                   s->world.d8105E0[2], &yaw));
        cput(cam, 0x44, yaw);
    } else {
        cam[1] = 0;
        FAULT_AT(s, 0x001B0300u, s->w.w_001B0300(s->w.ctx));
    }
    cam[2] = 0;                                                       /* 00197718 */
    cam[3] = 0;
    chput(cam, 8, 0);
    return 0;
}

/* ---- adapters ------------------------------------------------------------ */

int em_cam_specials_action_00195130(void *ctx, uint8_t *cam, EmPlayerLiveActor *player)
{
    return em_cam_specials_00195130((EmCamSpecials *)ctx, cam, player);
}

int em_cam_specials_action_001936E0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player)
{
    return em_cam_specials_001936E0((EmCamSpecials *)ctx, cam, player);
}

int em_cam_specials_call_00193EB0(void *ctx, uint8_t *cam, EmPlayerLiveActor *player,
                                  int32_t handled)
{
    return em_cam_specials_00193EB0((EmCamSpecials *)ctx, cam, player, handled);
}

int em_cam_specials_call_00197490(void *ctx, uint8_t *cam, EmPlayerLiveActor *player,
                                  int32_t a2)
{
    return em_cam_specials_00197490((EmCamSpecials *)ctx, cam, player, a2);
}
