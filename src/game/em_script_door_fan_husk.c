/* AREA11 husk creature 0x825940, husk partner 0x827490 and manager 0x823CE0
 * (see em_script_door_fan_husk.h and docs/SCRIPT_DOOR_FAN.md). Addresses in
 * comments are original runtime addresses (overlay listing address + 0x40);
 * float constants are the original bit patterns. */
#include "game/em_script_door_fan_husk.h"

#include <stddef.h>

#include "game/em_ee_float.h"

#define HUSK_SWING_RATE UINT32_C(0x3C84C3C4) /* +0x1F4 */
#define HUSK_SWING_STEP UINT32_C(0x3D37D3FB) /* +0x1F8 */
#define HUSK_W210 UINT32_C(0x3DD67750)       /* +0x210 and +0x218 */
#define HUSK_BONE_SCALE UINT32_C(0xBF543B67) /* setup: bone3 +0x78 = BIAS + SCALE * sin */
#define HUSK_BONE_BIAS UINT32_C(0xBF91361E)
#define HUSK_SWING_GAIN UINT32_C(0x3E9C61AA) /* lifecycle 2: bone3 +0x78 = SCALE + GAIN * sin */
#define HUSK_NEG_HALF_PI UINT32_C(0xBFC90FDB)
#define HUSK_NINETY UINT32_C(0x42B40000)
#define HUSK_QUARTER UINT32_C(0x3E800000)
#define HUSK_RANGE 300.0f                    /* 0x43960000, 001FBD50 f12 */

static int husk_fault(EmHuskFault *fault, uint32_t address, int32_t code)
{
    if (fault && fault->code == EM_HUSK_FAULT_NONE) {
        fault->address = address;
        fault->code = code;
    }
    return -1;
}

static int husk_check(EmHuskFault *fault, uint32_t callee, int result)
{
    return result < 0 ? husk_fault(fault, callee, EM_HUSK_FAULT_WORKER_FAILED) : 0;
}

#define NEED(ptr, address) \
    do { if (!(ptr)) return husk_fault(fault, (address), EM_HUSK_FAULT_NULL); } while (0)
#define CALL(address, expr) \
    do { if (husk_check(fault, (address), (expr)) < 0) return -1; } while (0)

static float bits_f(uint32_t b) { return em_ee_float(b); }
static uint32_t f_bits(float f) { return em_ee_bits(f); }

/* 32-bit arithmetic shift right (the sra the listing uses). */
static int32_t sra32(uint32_t value, unsigned shift)
{
    return (int32_t)(value >> shift) |
           (int32_t)((value & UINT32_C(0x80000000)) ? ~(UINT32_C(0xFFFFFFFF) >> shift) : 0);
}

/* 001BA1C0(actor, index): D_00810758[index] == 0xFF (byte-matched leaf). */
static int husk_flag_done(const EmHuskWorld *world, unsigned index, EmHuskFault *fault,
                          int *done)
{
    NEED(world && world->d810758, 0x00810758u + index);
    *done = world->d810758[index] == 0xFF;
    return 0;
}

/* The shared tail: 001C6380(actor), 001B17A0(actor), jalr *(actor + 0x4C). */
static int husk_tail(const EmHuskWorkers *w, EmHuskFault *fault, uint32_t owner)
{
    NEED(w->w_001C6380, 0x001C6380u);
    CALL(0x001C6380u, w->w_001C6380(w->ctx));
    NEED(w->w_001B17A0, 0x001B17A0u);
    CALL(0x001B17A0u, w->w_001B17A0(w->ctx));
    NEED(w->w_draw_4C, owner);
    CALL(owner, w->w_draw_4C(w->ctx));
    return 1;
}

static int husk_free(const EmHuskWorkers *w, EmHuskFault *fault, uint8_t *freed)
{
    NEED(w->w_001AFC10, 0x001AFC10u);
    CALL(0x001AFC10u, w->w_001AFC10(w->ctx));
    *freed = 1;
    return 0;
}

/* 001B0FD0 with the module's +0x04 stores. Returns its result (0/1) or -1. */
static int husk_setup_model(const EmHuskWorkers *w, EmHuskFault *fault, uint8_t *lifecycle)
{
    int32_t result;
    NEED(w->w_001B0FD0, 0x001B0FD0u);
    CALL(0x001B0FD0u, w->w_001B0FD0(w->ctx, &result));
    if (result == 0)
        *lifecycle = (uint8_t)(*lifecycle + 1); /* 001B0FD0 */
    else if (result == 1)
        *lifecycle = 3;                         /* 001B0EA0 */
    else
        return husk_fault(fault, 0x001B0FD0u, EM_HUSK_FAULT_WORKER_FAILED);
    return result;
}

/* *(float *)(*(D_00275B40 + 0xC) + 0x78) = value; returns the bone or -1. */
static int husk_bone3(const EmHuskWorkers *w, EmHuskFault *fault, uint32_t *bone)
{
    NEED(w->r_00275B40, 0x00275B40u);
    CALL(0x00275B40u, w->r_00275B40(w->ctx, 3, bone));
    return 0;
}

/* ---- creature 0x825940 --------------------------------------------------- */

/* Lifecycle 0 (0x8259AC..0x825B2C). */
static int husk_setup(EmHuskCreature *h, const EmHuskWorld *world, const EmHuskWorkers *w,
                      EmHuskFault *fault)
{
    int r = husk_setup_model(w, fault, &h->lifecycle);
    if (r != 0)
        return r < 0 ? -1 : 1; /* 0x8259B4: 001B0FD0 non-zero returns */
    int done;
    if (husk_flag_done(world, EM_HUSK_FLAG_30, fault, &done) < 0) /* 0x8259C0 */
        return -1;
    h->lifecycle = done ? 4 : 0x64; /* 0x8259C8..0x8259E0 */
    h->b00 = 1;                      /* 0x8259EC */

    /* 0x8259E8..0x825A10: +0x28 = 300 + ((300 * (rand >> 16)) >> 15). */
    int32_t random;
    NEED(w->w_00122BB8, 0x00122BB8u);
    CALL(0x00122BB8u, w->w_00122BB8(w->ctx, &random));
    uint32_t t = (uint32_t)sra32((uint32_t)random, 16);
    uint32_t v = (t << 4) - t;
    v = v + (v << 2);
    v = v << 2;
    h->timer_28 = (int16_t)(uint16_t)((uint32_t)sra32(v, 15) + 300u);

    /* 0x825A14..0x825A54, in store order. */
    h->w200 = 1;
    h->w204 = 0;
    h->w208 = 0;
    h->f210 = bits_f(HUSK_W210);
    h->f218 = bits_f(HUSK_W210);
    h->w20C = 0;
    h->f214 = 0.0f;
    h->f1F4 = bits_f(HUSK_SWING_RATE);
    h->f1F8 = bits_f(HUSK_SWING_STEP);
    h->f1FC = 0.0f;

    /* 0x825A58..0x825A90: bone3 +0x78 = BIAS + SCALE * sin(+0x1FC). */
    float s;
    NEED(w->w_0011E2A8, 0x0011E2A8u);
    CALL(0x0011E2A8u, w->w_0011E2A8(w->ctx, h->f1FC, &s));
    uint32_t angle = em_ee_add_bits(HUSK_BONE_BIAS, em_ee_mul_bits(HUSK_BONE_SCALE, f_bits(s)));
    uint32_t bone;
    if (husk_bone3(w, fault, &bone) < 0)
        return -1;
    NEED(w->s_bone_f32, bone + 0x78u);
    CALL(bone + 0x78u, w->s_bone_f32(w->ctx, bone, 0x78, bits_f(angle)));
    NEED(w->w_001C6380, 0x001C6380u);
    CALL(0x001C6380u, w->w_001C6380(w->ctx));
    /* 0x825A94..0x825AA4: 001A2370(actor, bone3 + 0x90), slot re-read. */
    if (husk_bone3(w, fault, &bone) < 0)
        return -1;
    NEED(w->w_001A2370, 0x001A2370u);
    CALL(0x001A2370u, w->w_001A2370(w->ctx, bone + 0x90u));

    /* 0x825AA8..0x825AC4: +0x220 = 0; child = 001AFA90(0xC). */
    h->child_220 = 0;
    uint32_t node = 0;
    EmHuskChild *c = NULL;
    NEED(w->w_001AFA90, 0x001AFA90u);
    CALL(0x001AFA90u, w->w_001AFA90(w->ctx, EM_HUSK_CHILD_CLASS, &node, &c));
    if (node == 0)
        return 1; /* 0x825AC0 */
    NEED(c, 0x001AFA90u);
    /* 0x825AC8..0x825B2C, in store order. */
    c->b9A = 0;
    c->b03 = 0;
    c->h2E = 0;
    c->b0D = EM_HUSK_CHILD_MODEL;
    c->h0E = 0xFFFF;
    c->h54 = 0;
    c->h56 = 0;
    c->fA0[0] = 0.0f;
    c->fA0[1] = 0.0f;
    c->fA0[2] = 0.0f;
    c->fA0[3] = bits_f(HUSK_QUARTER);
    for (int i = 0; i < 4; ++i)
        c->pos_B0[i] = h->pos_B0[i]; /* 00102948(child +0xB0, actor +0xB0) */
    for (int i = 0; i < 4; ++i)
        c->rot_C0[i] = h->rot_C0[i]; /* 00102948(child +0xC0, actor +0xC0) */
    c->handler_10 = EM_HUSK_CHILD_HANDLER;
    h->child_220 = node;
    h->w224 = 0;
    return 1;
}

/* Lifecycle 2 with D_00810788 == 0xFF (0x826D70..0x826ED4). */
static int husk_swing(EmHuskCreature *h, const EmHuskWorld *world, const EmHuskWorkers *w,
                      EmHuskFault *fault)
{
    uint32_t a = f_bits(h->f1FC);
    if (em_ee_c_eq_bits(HUSK_NEG_HALF_PI, a)) {
        /* 0x826E3C: count +0x21C down and drive the child's +0xA0 vector. */
        if (h->w21C <= 0)
            return 0;
        h->w21C = (int32_t)((uint32_t)h->w21C - 1u);
        uint32_t q = em_ee_div_bits(em_ee_cvt_s_w_bits((uint32_t)h->w21C), HUSK_NINETY);
        NEED(world->spad3A20, 0x70003A20u);
        *world->spad3A20 = bits_f(q);
        EmHuskChild *c = NULL;
        NEED(w->r_child_220, h->child_220 + 0xA0u);
        CALL(h->child_220 + 0xA0u, w->r_child_220(w->ctx, h->child_220, &c));
        NEED(c, h->child_220 + 0xA0u);
        if (h->w224 != 0) { /* 0x826E7C */
            c->fA0[0] = *world->spad3A20;
            c->fA0[1] = 0.0f;
        } else {            /* 0x826EAC */
            c->fA0[0] = 0.0f;
            c->fA0[1] = *world->spad3A20;
        }
        c->fA0[2] = 0.0f;
        c->fA0[3] = bits_f(HUSK_QUARTER);
        return 0;
    }
    /* 0x826D94 / 0x826DC8: step +0x1FC toward -pi/2 by +0x1F8, clamped. */
    if (em_ee_c_lt_bits(a, HUSK_NEG_HALF_PI)) {
        uint32_t next = em_ee_add_bits(a, f_bits(h->f1F8));
        h->f1FC = bits_f(em_ee_c_le_bits(next, HUSK_NEG_HALF_PI) ? next : HUSK_NEG_HALF_PI);
    } else {
        uint32_t next = em_ee_sub_bits(a, f_bits(h->f1F8));
        h->f1FC = bits_f(em_ee_c_lt_bits(next, HUSK_NEG_HALF_PI) ? HUSK_NEG_HALF_PI : next);
    }
    /* 0x826DE8..0x826E30: bone3 +0x78 = SCALE + GAIN * sin(+0x1FC); then
     * 00102958(child bone3 + 0x90, actor bone3 + 0x90). */
    float s;
    NEED(w->w_0011E2A8, 0x0011E2A8u);
    CALL(0x0011E2A8u, w->w_0011E2A8(w->ctx, h->f1FC, &s));
    uint32_t angle = em_ee_add_bits(HUSK_BONE_SCALE, em_ee_mul_bits(HUSK_SWING_GAIN, f_bits(s)));
    uint32_t bone;
    if (husk_bone3(w, fault, &bone) < 0)
        return -1;
    NEED(w->s_bone_f32, bone + 0x78u);
    CALL(bone + 0x78u, w->s_bone_f32(w->ctx, bone, 0x78, bits_f(angle)));
    EmHuskChild *c = NULL;
    NEED(w->r_child_220, h->child_220 + 0x11Cu);
    CALL(h->child_220 + 0x11Cu, w->r_child_220(w->ctx, h->child_220, &c));
    NEED(c, h->child_220 + 0x11Cu);
    NEED(w->w_00102958, 0x00102958u);
    CALL(0x00102958u, w->w_00102958(w->ctx, c->bone3_11C + 0x90u, h->bone3_11C + 0x90u));
    return 0;
}

int em_husk_creature_tick(EmHuskCreature *h, const EmHuskWorld *world,
                          const EmHuskWorkers *w, EmHuskFault *fault)
{
    if (!fault || fault->code != EM_HUSK_FAULT_NONE)
        return -1;
    NEED(h && w, EM_HUSK_CREATURE);
    if (h->freed)
        return husk_fault(fault, EM_HUSK_CREATURE, EM_HUSK_FAULT_FREED);
    int done;
    switch (h->lifecycle) {
    case 0:
        return husk_setup(h, world, w, fault);
    case 0x64: /* 0x825B34: dormant; the flag moves it to lifecycle 4. */
        if (husk_flag_done(world, EM_HUSK_FLAG_30, fault, &done) < 0)
            return -1;
        if (done)
            h->lifecycle = 4;
        return husk_tail(w, fault, EM_HUSK_CREATURE);
    case 2: /* 0x826D60 */
        if (husk_flag_done(world, EM_HUSK_FLAG_30, fault, &done) < 0)
            return -1;
        if (done && husk_swing(h, world, w, fault) < 0)
            return -1;
        return husk_tail(w, fault, EM_HUSK_CREATURE);
    case 1: /* 0x826190: not translated (see the header). */
        return husk_fault(fault, 0x00826190u, EM_HUSK_FAULT_UNTRANSLATED);
    case 4: /* 0x825B74: not translated (see the header). */
        return husk_fault(fault, 0x00825B74u, EM_HUSK_FAULT_UNTRANSLATED);
    default: /* 3 (0x825964) and every other value (0x8259A4): free. */
        return husk_free(w, fault, &h->freed);
    }
}

/* ---- partner 0x827490 ---------------------------------------------------- */

static int husk_linked(const EmHuskWorkers *w, EmHuskFault *fault, EmHuskLinked **linked)
{
    NEED(w->r_link_18, EM_HUSK_PARTNER);
    CALL(EM_HUSK_PARTNER, w->r_link_18(w->ctx, linked));
    NEED(*linked, EM_HUSK_PARTNER);
    return 0;
}

int em_husk_partner_tick(EmHuskPartner *p, const EmHuskWorkers *w, EmHuskFault *fault)
{
    if (!fault || fault->code != EM_HUSK_FAULT_NONE)
        return -1;
    NEED(p && w, EM_HUSK_PARTNER);
    if (p->freed)
        return husk_fault(fault, EM_HUSK_PARTNER, EM_HUSK_FAULT_FREED);
    EmHuskLinked *linked = NULL;
    int32_t result;
    switch (p->lifecycle) {
    case 0: { /* 0x8274D4 */
        int r = husk_setup_model(w, fault, &p->lifecycle);
        if (r != 0)
            return r < 0 ? -1 : 1;
        p->h34 = 1; /* 0x8274E8 */
        p->b00 = 1;
        NEED(w->w_001B11E0, 0x001B11E0u);
        CALL(0x001B11E0u, w->w_001B11E0(w->ctx, p->item_9A, &result));
        if (result == 0)
            return 1; /* 0x8274F8 */
        if (husk_linked(w, fault, &linked) < 0)
            return -1;
        linked->lifecycle = 2; /* 0x82750C */
        p->lifecycle = 3;
        return 1;
    }
    case 1: /* 0x827518 */
        if (p->hit_36 == 0)
            return husk_tail(w, fault, EM_HUSK_PARTNER);
        p->b00 = 2;
        if (husk_linked(w, fault, &linked) < 0)
            return -1;
        linked->lifecycle = 2; /* 0x82753C */
        linked->w21C = 0x5A;   /* 0x827548 */
        NEED(w->w_001EFE00, 0x001EFE00u);
        CALL(0x001EFE00u, w->w_001EFE00(w->ctx, EM_HUSK_PARTNER_FX, &result));
        if (result == 0) {
            p->lifecycle = 3; /* 0x827580 */
            return 1;
        }
        NEED(w->w_001FBD50, 0x001FBD50u);
        CALL(0x001FBD50u, w->w_001FBD50(w->ctx, EM_HUSK_PARTNER_HIT_SOUND, 0, HUSK_RANGE));
        p->timer_28 = 0;
        p->lifecycle = 2;
        return 1;
    case 2: /* 0x8275AC */
        if (p->timer_28 < 10) {
            p->timer_28 = (int16_t)(p->timer_28 + 1);
            if (p->timer_28 == 10) {
                NEED(w->w_001FBD50, 0x001FBD50u);
                CALL(0x001FBD50u,
                     w->w_001FBD50(w->ctx, EM_HUSK_PARTNER_FALL_SOUND, 0, HUSK_RANGE));
            }
        }
        NEED(w->w_001B1190, 0x001B1190u);
        CALL(0x001B1190u, w->w_001B1190(w->ctx, p->item_9A)); /* 0x8275E8 */
        NEED(w->w_001B17A0, 0x001B17A0u);
        CALL(0x001B17A0u, w->w_001B17A0(w->ctx));
        NEED(w->w_draw_4C, EM_HUSK_PARTNER);
        CALL(EM_HUSK_PARTNER, w->w_draw_4C(w->ctx));
        return 1;
    default: /* 3 (0x82760C) and every other value (0x8274CC): free. */
        return husk_free(w, fault, &p->freed);
    }
}

/* ---- manager 0x823CE0 ---------------------------------------------------- */

int em_husk_manager_tick(EmHuskManager *m, const EmHuskWorld *world, const EmHuskWorkers *w,
                         EmHuskFault *fault)
{
    if (!fault || fault->code != EM_HUSK_FAULT_NONE)
        return -1;
    NEED(m && w, EM_HUSK_MANAGER);
    if (m->freed)
        return husk_fault(fault, EM_HUSK_MANAGER, EM_HUSK_FAULT_FREED);
    int done;
    int32_t result;
    switch (m->lifecycle) {
    case 2:
    case 3: /* 0x823CF8 / 0x823D04 -> 0x823E68: 001AFC10(actor) */
        return husk_free(w, fault, &m->freed);
    case 0: /* 0x823D2C: 001BA1C0(actor, 0x30) */
        if (husk_flag_done(world, EM_HUSK_FLAG_30, fault, &done) < 0)
            return -1;
        if (done) {
            m->lifecycle = 3;
        } else {
            m->lifecycle = 1;
            m->b00 = 1;
        }
        return 1;
    case 1:
        break;
    default: /* 0x823D20: returns without a call */
        return 1;
    }

    if (m->phase == 1) {
        /* 0x823DDC: poll the script; on completion restore and end. */
        NEED(w->w_001BA1F0, 0x001BA1F0u);
        CALL(0x001BA1F0u, w->w_001BA1F0(w->ctx, &result));
        if (result != 0) {
            NEED(world && world->d8106C8, 0x008106C8u);
            *world->d8106C8 |= 0x40u;
            NEED(w->w_001D2830, 0x001D2830u);
            CALL(0x001D2830u, w->w_001D2830(w->ctx, 8, 0));
            NEED(w->w_001C1DC0, 0x001C1DC0u);
            CALL(0x001C1DC0u, w->w_001C1DC0(w->ctx));
            NEED(w->w_001AEE10, 0x001AEE10u);
            CALL(0x001AEE10u, w->w_001AEE10(w->ctx, 4, 0));
            NEED(w->w_001C4760, 0x001C4760u);
            CALL(0x001C4760u, w->w_001C4760(w->ctx, 0x1A, 1));
            NEED(w->w_001FAE70, 0x001FAE70u);
            CALL(0x001FAE70u, w->w_001FAE70(w->ctx, 0));
            m->h2E = 0xFFFF;
            NEED(world->d810808, 0x00810808u);
            *world->d810808 = 0xFF;
            m->lifecycle = 3;
        }
    } else if (m->phase == 0) {
        /* 0x823D74: D_00810788 != 0 starts script 0x828C70. */
        NEED(world && world->d810758, 0x00810788u);
        if (world->d810758[EM_HUSK_FLAG_30] != 0) {
            m->h28 = 0;
            NEED(w->w_001BA1A0, 0x001BA1A0u);
            CALL(0x001BA1A0u, w->w_001BA1A0(w->ctx, EM_HUSK_MANAGER_SCRIPT));
            m->phase = 1;
            NEED(world->d8106C8, 0x008106C8u);
            *world->d8106C8 &= UINT32_C(0xF1FFFF8F);
            NEED(w->w_001D2830, 0x001D2830u);
            CALL(0x001D2830u, w->w_001D2830(w->ctx, 8, 1));
            NEED(w->w_001C1DC0, 0x001C1DC0u);
            CALL(0x001C1DC0u, w->w_001C1DC0(w->ctx));
            NEED(w->w_001FABB0, 0x001FABB0u);
            CALL(0x001FABB0u, w->w_001FABB0(w->ctx));
        }
    }
    /* 0x823E50 / 0x823E54: 001B17A0(actor) */
    NEED(w->w_001B17A0, 0x001B17A0u);
    CALL(0x001B17A0u, w->w_001B17A0(w->ctx));
    return 1;
}
