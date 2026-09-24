/* Script-host and door leaves (see em_script_door_fan.h and
 * docs/SCRIPT_DOOR_FAN.md). Addresses in comments are original runtime
 * addresses of the pinned boot ELF; each branch and store cites the address
 * it comes from. */
#include "game/em_script_door_fan.h"

#include <stddef.h>
#include <string.h>

#include "game/em_ee_float.h"

static int sdf_fault(EmSdfFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_SDF_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

/* Worker result check: a negative result latches WORKER_FAILED at `callee`. */
static int sdf_check(EmSdfFault *fault, uint32_t callee, int result)
{
    return result < 0 ? sdf_fault(fault, callee, EM_SDF_FAULT_WORKER_FAILED) : 0;
}

#define NEED(ptr, address) \
    do { if (!(ptr)) return sdf_fault(fault, (address), EM_SDF_FAULT_NULL); } while (0)
#define CALL(address, expr) \
    do { if (sdf_check(fault, (address), (expr)) < 0) return -1; } while (0)

static int sdf_latched(const EmSdfFault *fault)
{
    return !fault || fault->code != EM_SDF_FAULT_NONE;
}

static int16_t rd16(const uint8_t *p, unsigned at)
{
    return (int16_t)(uint16_t)(p[at] | (unsigned)p[at + 1] << 8);
}

static uint32_t rd32(const uint8_t *p, unsigned at)
{
    return (uint32_t)p[at] | (uint32_t)p[at + 1] << 8 | (uint32_t)p[at + 2] << 16 |
           (uint32_t)p[at + 3] << 24;
}

static float rdf(const uint8_t *p, unsigned at)
{
    uint32_t bits = rd32(p, at);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* The bytes [address, address + length) of the image, or NULL. */
static const uint8_t *sdf_image(const EmSdfImage *image, uint32_t address, uint32_t length)
{
    if (!image || !image->bytes || address < image->base || length > image->length ||
        address - image->base > image->length - length)
        return NULL;
    return image->bytes + (address - image->base);
}

/* ---- 001BA510 ------------------------------------------------------------ */

int em_sdf_001BA510(uint8_t activity[12], EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(activity, 0x008106D4u);
    /* 0x1BA51C..0x1BA530: i = 1..12, D_008106B0[0x24 + i - 1] = 0. */
    for (int i = 0; i < 12; ++i)
        activity[i] = 0;
    return 0;
}

/* ---- 001BAC00 ------------------------------------------------------------ */

int em_sdf_001BAC00(EmSdfSpawnOwner *owner, const uint8_t *record, const EmSdfImage *image,
                    const EmSdfWorkers *w, EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(owner, 0x001BAC00u);
    NEED(record, 0x001BAC00u);
    NEED(w, 0x001BAC00u);

    uint32_t entry = rd32(record, 0x14); /* e = *(arg2 + 0x14) */
    int32_t idx = 0;
    const uint8_t *e;
    do {
        e = sdf_image(image, entry, EM_SDF_ENTRY_SIZE);
        if (!e)
            return sdf_fault(fault, entry, EM_SDF_FAULT_OUT_OF_IMAGE);
        uint32_t node = 0;
        EmSdfSpawned *r = NULL;
        if (rd16(e, 4) == EM_SDF_TAG_ALT_SPAWN) {
            /* r = 001C8140(D_0028A490[e +6], e +8, e +0x28) */
            uint32_t bank;
            NEED(w->r_0028A490, EM_SDF_D_0028A490);
            CALL(EM_SDF_D_0028A490, w->r_0028A490(w->ctx, rd16(e, 6), &bank));
            NEED(w->w_001C8140, 0x001C8140u);
            CALL(0x001C8140u, w->w_001C8140(w->ctx, bank, rd16(e, 8), rd32(e, 0x28), &node, &r));
            if (node != 0) {
                NEED(r, 0x001C8140u);
                r->owner_24 = owner->self_14; /* r +0x24 = arg0 +0x14 */
                r->s2E = 0xF;                  /* r +0x2E = 0xF */
            }
        } else {
            /* r = 001AFA90(e +0 byte) */
            NEED(w->w_001AFA90, 0x001AFA90u);
            CALL(0x001AFA90u, w->w_001AFA90(w->ctx, e[0], &node, &r));
            if (node != 0) {
                NEED(r, 0x001AFA90u);
                r->b03 = e[2];
                r->b0D = (uint8_t)rd16(e, 4);
                r->pos_B0[0] = rdf(e, 0x10);
                r->pos_B0[1] = rdf(e, 0x14);
                r->pos_B0[2] = rdf(e, 0x18);
                r->rot_C0[0] = rdf(e, 0x1C);
                r->rot_C0[1] = rdf(e, 0x20);
                r->rot_C0[2] = rdf(e, 0x24);
                /* +0x10 = e +0x28 when e +0xA != 3 and it is non-zero,
                 * otherwise 001BB0E0. */
                uint32_t handler = rd32(e, 0x28);
                r->handler_10 = rd16(e, 0xA) != 3 && handler != 0 ? handler
                                                                    : EM_SDF_DEFAULT_HANDLER;
                r->entry_20 = entry;
                r->owner_24 = owner->self_14;
                r->s2E = (int16_t)idx;
            }
            idx += 1;
        }
        entry += EM_SDF_ENTRY_SIZE;
        e = sdf_image(image, entry, 2);
        if (!e)
            return sdf_fault(fault, entry, EM_SDF_FAULT_OUT_OF_IMAGE);
    } while (rd16(e, 0) != -1);
    owner->s2E = 0; /* arg0 +0x2E = 0 */
    return 1;
}

/* ---- 001BAD40 ------------------------------------------------------------ */

/* The common tail 0x1BAF00..0x1BB0B0 (command != 3). */
static int sdf_bad40_tail(EmSdfEventActor *obj, const uint8_t *ev, int16_t cmd,
                          const EmSdfWorld *world, const EmSdfWorkers *w, EmSdfFault *fault)
{
    /* 0x1BAF00: +0x40 = D_0028A490[ev +6]; +0x0C = 001C6150(+0x44). */
    uint32_t bank;
    NEED(w->r_0028A490, EM_SDF_D_0028A490);
    CALL(EM_SDF_D_0028A490, w->r_0028A490(w->ctx, rd16(ev, 6), &bank));
    obj->bank_40 = bank;
    int32_t count;
    NEED(w->w_001C6150, 0x001C6150u);
    CALL(0x001C6150u, w->w_001C6150(w->ctx, obj->w44, &count));
    NEED(world->d275BCC, 0x00275BCCu);
    if ((uint8_t)count > EM_SDF_BONE_SLOTS && !(*world->d275BCC < (int)(uint8_t)count))
        return sdf_fault(fault, 0x001BAF58u, EM_SDF_FAULT_CAPACITY);
    obj->b0C = (uint8_t)count;
    /* 0x1BAF28: the signed budget below the count rejects the actor. */
    if (*world->d275BCC < (int)obj->b0C) {
        obj->lifecycle = 3;
        return 1;
    }
    /* 0x1BAF58..0x1BAF7C: one 001AF780 handle per bone into +0x110. */
    for (int i = 0; i < (int)obj->b0C; ++i) {
        uint32_t handle;
        NEED(w->w_001AF780, 0x001AF780u);
        CALL(0x001AF780u, w->w_001AF780(w->ctx, &handle));
        obj->bones_110[i] = handle;
    }
    obj->b09 = obj->b0C; /* 0x1BAF80 */

    /* 0x1BAF84: second dispatch on the command. */
    uint8_t mode;
    if (cmd == 8) {
        mode = 1;
    } else if (cmd == 6 || cmd == 0) {
        if (cmd == 6) {
            NEED(w->w_001D8BF0, 0x001D8BF0u);
            CALL(0x001D8BF0u, w->w_001D8BF0(w->ctx, obj, 1));
        } else {
            NEED(w->w_001BA8E0, 0x001BA8E0u);
            CALL(0x001BA8E0u, w->w_001BA8E0(w->ctx, obj, rd16(ev, 4)));
        }
        mode = obj->b09 < 3 ? 0 : 2; /* 0x1BAFC0 / 0x1BB004 */
    } else {
        mode = 0;
    }
    NEED(w->w_001CA6F0, 0x001CA6F0u);
    CALL(0x001CA6F0u, w->w_001CA6F0(w->ctx, obj, mode));

    /* 0x1BB05C: anim_bone_array_setup(+9); bone_init_default_2(obj, ev +8). */
    NEED(w->w_001CB5B0, 0x001CB5B0u);
    CALL(0x001CB5B0u, w->w_001CB5B0(w->ctx, obj->b09));
    NEED(w->w_001C63E0, 0x001C63E0u);
    CALL(0x001C63E0u, w->w_001C63E0(w->ctx, obj, rd16(ev, 8)));
    if (rd16(ev, 0xA) != 4) /* 0x1BB078 */
        return 0;
    /* 0x1BB080..0x1BB0A4: anim_clip_init(obj, ev +8, 0, (float)(001C61D0 - 1)). */
    int32_t frames;
    NEED(w->w_001C61D0, 0x001C61D0u);
    CALL(0x001C61D0u, w->w_001C61D0(w->ctx, obj->bank_40, rd16(ev, 8), &frames));
    float length = em_ee_float(em_ee_cvt_s_w_bits((uint32_t)(frames - 1)));
    NEED(w->w_001C67E0, 0x001C67E0u);
    CALL(0x001C67E0u, w->w_001C67E0(w->ctx, rd16(ev, 8), 0.0f, length));
    return 0;
}

int em_sdf_001BAD40(EmSdfEventActor *obj, const uint8_t *ev, const EmSdfWorld *world,
                    const EmSdfWorkers *w, EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(obj, 0x001BAD40u);
    NEED(ev, 0x001BAD40u);
    NEED(world, 0x001BAD40u);
    NEED(w, 0x001BAD40u);

    int16_t msg = rd16(ev, 4);
    if (msg == EM_SDF_MSG_REQUEST) {
        /* 0x1BAD70..0x1BADA0 */
        NEED(world->d2821B0, 0x002821B0u);
        NEED(world->d2821B4, 0x002821B4u);
        NEED(world->d2821B8, 0x002821B8u);
        *world->d2821B0 = 2;
        *world->d2821B4 = 1;
        *world->d2821B8 = rd16(ev, 8);
        obj->b0C = 0;
        obj->b09 = 0;
        return 0;
    }
    if (msg == EM_SDF_MSG_PUBLISH) {
        /* 0x1BADB0 */
        NEED(world->d8106C0, 0x008106C0u);
        *world->d8106C0 = obj->w18;
        return 0;
    }

    /* 0x1BADC4: jump table 0x26E170 for commands 0..8; any other value
     * (unsigned compare) goes to the tail. */
    int16_t cmd = rd16(ev, 0xA);
    uint32_t bank, model;
    switch ((uint16_t)cmd < 9 ? cmd : -1) {
    case 0: case 4: case 6: /* 0x1BADF0: 001CA6E0(obj, D_0028A490[msg]) */
        NEED(w->r_0028A490, EM_SDF_D_0028A490);
        CALL(EM_SDF_D_0028A490, w->r_0028A490(w->ctx, msg, &bank));
        NEED(w->w_001CA6E0, 0x001CA6E0u);
        CALL(0x001CA6E0u, w->w_001CA6E0(w->ctx, obj, bank));
        break;
    case 1: case 8: case 2: /* 0x1BAE14 / 0x1BAE34: 001C6120(D_0028A59C or 56C, msg) */
        NEED(w->r_0028A490, EM_SDF_D_0028A490);
        CALL(EM_SDF_D_0028A490,
             w->r_0028A490(w->ctx, cmd == 2 ? EM_SDF_INDEX_0028A56C : EM_SDF_INDEX_0028A59C,
                           &bank));
        NEED(w->w_001C6120, 0x001C6120u);
        CALL(0x001C6120u, w->w_001C6120(w->ctx, bank, msg, &model));
        NEED(w->w_001CA6E0, 0x001CA6E0u);
        CALL(0x001CA6E0u, w->w_001CA6E0(w->ctx, obj, model));
        break;
    case 3: { /* 0x1BAE54..0x1BAEB8, then 0x1BB0B4 */
        NEED(w->r_0028A490, EM_SDF_D_0028A490);
        CALL(EM_SDF_D_0028A490, w->r_0028A490(w->ctx, rd16(ev, 6), &bank));
        NEED(w->w_001C6120, 0x001C6120u);
        CALL(0x001C6120u, w->w_001C6120(w->ctx, bank, rd16(ev, 8), &model));
        NEED(world->d810250, 0x00810250u);
        NEED(world->d810254, 0x00810254u);
        NEED(world->d810258, 0x00810258u);
        NEED(world->d8101E4, 0x008101E4u);
        NEED(world->d81024E, 0x0081024Eu);
        *world->d810250 = model;
        *world->d810254 = 0.0f;
        float head;
        NEED(w->r_track_head, model);
        CALL(model, w->r_track_head(w->ctx, *world->d810250, &head));
        *world->d810258 = head;
        *world->d8101E4 = 3;
        *world->d81024E = (int16_t)rd32(ev, 0x28);
        NEED(w->w_0022EC30, 0x0022EC30u);
        CALL(0x0022EC30u, w->w_0022EC30(w->ctx, EM_SDF_D_008101E0));
        obj->b0C = 0;
        obj->b09 = 0;
        return 0;
    }
    case 5: /* 0x1BAEC4: 001C5C90(obj); result = (+4 >= 2) */
        NEED(w->w_001C5C90, 0x001C5C90u);
        CALL(0x001C5C90u, w->w_001C5C90(w->ctx, obj));
        return obj->lifecycle < 2 ? 0 : 1;
    default: /* case 7 (0x1BAEF0) and commands >= 9 */
        break;
    }
    return sdf_bad40_tail(obj, ev, rd16(ev, 0xA), world, w, fault);
}

/* ---- 001B1B30 ------------------------------------------------------------ */

int em_sdf_001B1B30(uint8_t *visible, float x, float y, float z, const EmSdfWorkers *w,
                    EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(visible, 0x001B1B30u);
    NEED(w, 0x001B1B30u);
    int32_t result;
    NEED(w->w_001B1630, 0x001B1630u);
    CALL(0x001B1630u, w->w_001B1630(w->ctx, x, y, z, &result));
    *visible = (uint8_t)result; /* 0x1B1B44: the byte store */
    if (*visible != 0) {        /* 0x1B1B48 */
        NEED(w->w_001B1B70, 0x001B1B70u);
        CALL(0x001B1B70u, w->w_001B1B70(w->ctx));
    }
    return *visible; /* 0x1B1B5C reloads the byte */
}

/* ---- 001BC240 / 001BC290 ------------------------------------------------- */

static int sdf_advance(EmSdfDoorStep *door, const EmSdfWorkers *w, EmSdfFault *fault)
{
    int16_t flags;
    NEED(w->w_001C64F0, 0x001C64F0u);
    CALL(0x001C64F0u, w->w_001C64F0(w->ctx, 1.0f, &flags));
    door->anim_flags = flags; /* block +0x0E */
    return 0;
}

int em_sdf_001BC240(EmSdfDoorStep *door, const EmSdfWorkers *w, EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(door, 0x001BC240u);
    NEED(w, 0x001BC240u);
    if (sdf_advance(door, w, fault) < 0)
        return -1;
    NEED(w->w_001BC150, 0x001BC150u);
    CALL(0x001BC150u, w->w_001BC150(w->ctx));
    return 0;
}

int em_sdf_001BC290(EmSdfDoorStep *door, const EmSdfWorld *world, const EmSdfWorkers *w,
                    EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(door, 0x001BC290u);
    NEED(world, 0x001BC290u);
    NEED(w, 0x001BC290u);
    if (sdf_advance(door, w, fault) < 0)
        return -1;
    NEED(world->d8106B8, 0x008106B8u);
    if (*world->d8106B8 != 0)
        return 0;
    NEED(w->w_001C67E0, 0x001C67E0u);
    CALL(0x001C67E0u, w->w_001C67E0(w->ctx, 0, 0.0f, 0.0f));
    door->b0B = 0;
    return 1;
}

/* ---- 001BBD60 ------------------------------------------------------------ */

int em_sdf_001BBD60(int16_t link_56, uint16_t side_2E, uint32_t *record_18,
                    const EmSdfWorkers *w, EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(record_18, 0x001BBD60u);
    NEED(w, 0x001BBD60u);
    uint32_t row = (uint32_t)(((int32_t)link_56 & 0xFF00) >> 8) * 4u;
    uint32_t address = EM_SDF_D_0024DB80 + row + (uint32_t)side_2E * 2u;
    uint16_t value;
    NEED(w->r_0024DB80, EM_SDF_D_0024DB80);
    CALL(EM_SDF_D_0024DB80, w->r_0024DB80(w->ctx, address, &value));
    *record_18 = value; /* a1[6] = zero-extended halfword */
    return 0;
}

/* ---- 001B0080 ------------------------------------------------------------ */

#define SEAT_EYE_X UINT32_C(0xC089999A)  /* -4.3 */
#define SEAT_EYE_Y UINT32_C(0x41AD999A)  /* 21.7 */
#define SEAT_EYE_Z UINT32_C(0xC40F2000)  /* -572.5 */
#define SEAT_TGT_X UINT32_C(0x421ECCCD)  /* 39.7 */
#define SEAT_TGT_Y UINT32_C(0x4180CCCD)  /* 16.1 */
#define SEAT_TGT_Z UINT32_C(0xC40B6000)  /* -557.5 */
#define SEAT_ONE UINT32_C(0x3F800000)
#define SEAT_LIFT UINT32_C(0x41880000)   /* 17.0 */

static void quad(float dst[4], const float src[4]) { memcpy(dst, src, 4 * sizeof(float)); }
static float fbits(uint32_t b) { return em_ee_float(b); }

int em_sdf_001B0080(EmSdfSeat *cam, float a1, const EmSdfSeatWorld *world,
                    const EmSdfWorkers *w, EmSdfFault *fault)
{
    if (sdf_latched(fault))
        return -1;
    NEED(cam, 0x001B0080u);
    NEED(world, 0x001B0080u);
    NEED(w, 0x001B0080u);
    NEED(world->d810700, 0x00810700u);
    NEED(world->d8105D0, 0x008105D0u);
    NEED(world->d8105E0, 0x008105E0u);
    if (*world->d810700 == 1) {
        NEED(world->d810702, 0x00810702u);
    }
    if (*world->d810700 == 1 && *world->d810702 == 4) {
        /* 0x1B00BC..0x1B0130: the fixed seat, then both working quads. */
        cam->eye_10[0] = fbits(SEAT_EYE_X);
        cam->eye_10[1] = fbits(SEAT_EYE_Y);
        cam->eye_10[2] = fbits(SEAT_EYE_Z);
        cam->eye_10[3] = fbits(SEAT_ONE);
        cam->tgt_20[0] = fbits(SEAT_TGT_X);
        cam->tgt_20[1] = fbits(SEAT_TGT_Y);
        cam->tgt_20[2] = fbits(SEAT_TGT_Z);
        cam->tgt_20[3] = fbits(SEAT_ONE);
        quad(world->d8105E0, cam->tgt_20);
        quad(world->d8105D0, cam->eye_10);
        return 0;
    }
    NEED(world->d810350, 0x00810350u);
    NEED(world->spad3B50, 0x70003B50u);
    NEED(world->spad3400, 0x70003400u);
    NEED(world->spad3600, 0x70003600u);
    /* 0x1B0138..0x1B0168: target = player +0xA0 quad, y += 17. */
    quad(cam->tgt_20, world->d810350);
    cam->tgt_20[1] = fbits(em_ee_add_bits(em_ee_bits(cam->tgt_20[1]), SEAT_LIFT));
    /* 0x1B0158..0x1B0180: angles = 0x70003B50 quad; +0x34 = 001B1470(+0x34). */
    quad(cam->rot_30, world->spad3B50);
    float wrapped;
    NEED(w->w_001B1470, 0x001B1470u);
    CALL(0x001B1470u, w->w_001B1470(w->ctx, cam->rot_30[1], &wrapped));
    cam->rot_30[1] = wrapped;
    /* 0x1B017C..0x1B0194: identity, then rotate by the angles. */
    NEED(w->w_001029C0, 0x001029C0u);
    CALL(0x001029C0u, w->w_001029C0(w->ctx, world->spad3400));
    NEED(w->w_00102C58, 0x00102C58u);
    CALL(0x00102C58u, w->w_00102C58(w->ctx, world->spad3400, world->spad3400, cam->rot_30));
    /* 0x1B019C..0x1B01D8: 0x70003600 = (0, 0, +0x0C, 1); +0x10 = m x v. */
    world->spad3600[0] = 0;
    world->spad3600[1] = 0;
    world->spad3600[2] = em_ee_bits(cam->f0C);
    world->spad3600[3] = SEAT_ONE;
    NEED(w->w_001026A0, 0x001026A0u);
    CALL(0x001026A0u, w->w_001026A0(w->ctx, cam->eye_10, world->spad3400, world->spad3600));
    /* 0x1B01DC..0x1B021C: eye += target, y also + a1 (a1 added to +0x24 first). */
    cam->eye_10[0] = fbits(em_ee_add_bits(em_ee_bits(cam->eye_10[0]), em_ee_bits(cam->tgt_20[0])));
    uint32_t lift = em_ee_add_bits(em_ee_bits(cam->tgt_20[1]), em_ee_bits(a1));
    cam->eye_10[1] = fbits(em_ee_add_bits(em_ee_bits(cam->eye_10[1]), lift));
    cam->eye_10[2] = fbits(em_ee_add_bits(em_ee_bits(cam->eye_10[2]), em_ee_bits(cam->tgt_20[2])));
    quad(world->d8105E0, cam->tgt_20);
    quad(world->d8105D0, cam->eye_10);
    return 0;
}
