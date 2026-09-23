/* AREA11 fan pair, overlay behaviour 0x827630 (see em_fan_original.h and
 * docs/FAN_ORIGINAL.md). Addresses in comments are original runtime
 * addresses; float constants are the original bit patterns. */
#include "game/em_fan_original.h"

#include <string.h>

#include "game/em_pose_math.h" /* EE add/sub model (pose_add/pose_sub) */

#define FAN_QUARTER_PI 0x1.921fb6p-1f  /* 0x3F490FDB, lifecycle 0 rot.z */
#define FAN_HOLD_END 0x1.d52500p+0f    /* 0x3FEA9280, phase 3 end rot.z */
#define FAN_STEP 0x1.7d45e4p-9f        /* 0x3B3EA2F2, phase 2/4 spin step */
#define FAN_SPIN_MAX 0x1.657186p-2f    /* 0x3EB2B8C3, phase 2 end */
#define FAN_SLOW 0x1.1df46ap-5f        /* 0x3D0EFA35, box arm selector */
#define FAN_WAIT 60                    /* 0x3C, phase 0 */
#define FAN_HOLD 30                    /* 0x1E, phase 2 end */
#define FAN_SOUND_RANGE 300.0f         /* 0x43960000, 001FBD50 f12 */
#define FAN_BOX_Y0 280.0f              /* 0x438C0000 */
#define FAN_BOX_Y1 320.0f              /* 0x43A00000 */
#define FAN_BOX_X0 318.0f              /* 0x439F0000 */
#define FAN_BOX_X1 340.0f              /* 0x43AA0000 */
#define FAN_EXIT_Z 156.0f              /* 0x431C0000 */
#define FAN_HIT_Z 166.5f               /* 0x43268000 */
#define FAN_HIT_F224 5.0f              /* 0x40A00000 */

static int fan_fault(EmFanOriginalFault *fault, uint32_t address, int32_t code)
{
    fault->address = address;
    fault->code = code;
    return -1;
}

/* Worker result check: negative -> WORKER_FAILED at `callee`. */
static int fan_leave(EmFanOriginalFault *fault, uint32_t callee, int result)
{
    return result < 0 ? fan_fault(fault, callee, EM_FAN_FAULT_WORKER_FAILED) : result;
}

void em_fan_original_spawn(EmFanOriginal *fan, uint16_t flags2, float placement_rot_z)
{
    memset(fan, 0, sizeof *fan);
    fan->flags2 = flags2;
    fan->rot_z = placement_rot_z;
}

/* 001B1470 (byte-matched C): while > pi subtract 2pi; while <= -pi add 2pi. */
float em_fan_original_wrap_001B1470(float angle)
{
    while (angle > 3.1415927f)
        angle = pose_sub(angle, 6.2831855f);
    while (angle <= -3.1415927f)
        angle = pose_add(angle, 6.2831855f);
    return angle;
}

/* jal sites 0x82798C/0x827AB4: 001B0C60(1,1,4) when D_00810758 == 0xFF, else
 * D_008107D8 |= 0x80. */
static int fan_exit_or_bit(EmFanOriginalGlobals *g, const EmFanOriginalWorkers *w,
                           EmFanOriginalFault *fault)
{
    if (g->d810758 != 0xFF) {
        g->d8107D8 = (uint8_t)(g->d8107D8 | 0x80);
        return 0;
    }
    if (!w->w_001B0C60)
        return fan_fault(fault, 0x001B0C60u, EM_FAN_FAULT_NULL_WORKER);
    return fan_leave(fault, 0x001B0C60u,
                     w->w_001B0C60(w->ctx, EM_FAN_ORIGINAL_EXIT_A0, EM_FAN_ORIGINAL_EXIT_A1,
                                   EM_FAN_ORIGINAL_EXIT_A2));
}

/* The four ordered c.le/c.lt tests on player +0xA4 then +0xA0. */
static int fan_in_box(const EmFanOriginalPlayer *p)
{
    return !(p->pos[1] <= FAN_BOX_Y0) && p->pos[1] < FAN_BOX_Y1 &&
           !(p->pos[0] <= FAN_BOX_X0) && p->pos[0] < FAN_BOX_X1;
}

/* 0x827890..0x827AD8: the flags2 == 1 box, reached only when B8 == 0. */
static int fan_box(EmFanOriginal *fan, EmFanOriginalPlayer *p, EmFanOriginalGlobals *g,
                   const EmFanOriginalWorkers *w, EmFanOriginalFault *fault)
{
    if (fan->spin < FAN_SLOW) {
        /* Slow arm (0x827A04): no publication, no +0x00 gate, no hit. */
        if (!p)
            return fan_fault(fault, EM_FAN_ORIGINAL_D_008102B0, EM_FAN_FAULT_NULL_WORKER);
        if (fan_in_box(p) && p->pos[2] < FAN_EXIT_Z)
            return fan_exit_or_bit(g, w, fault);
        return 0;
    }
    if (!w->w_001B17A0)
        return fan_fault(fault, 0x001B17A0u, EM_FAN_FAULT_NULL_WORKER);
    if (fan_leave(fault, 0x001B17A0u, w->w_001B17A0(w->ctx)) < 0)
        return -1;
    if (!p)
        return fan_fault(fault, EM_FAN_ORIGINAL_D_008102B0, EM_FAN_FAULT_NULL_WORKER);
    if (p->b00 != 1 || !fan_in_box(p))
        return 0;
    if (p->pos[2] < FAN_EXIT_Z)
        return fan_exit_or_bit(g, w, fault);
    if (p->pos[2] < FAN_HIT_Z) {
        /* 0x8279D4..0x827A00 */
        p->f224 = FAN_HIT_F224;
        p->b00 = 3;
        p->b0F = 6;
        p->f70[0] = 0.0f;
        p->f70[1] = 0.0f;
        p->f70[2] = 1.0f;
        p->f70[3] = 1.0f;
    }
    return 0;
}

/* Lifecycle 1: phase step (0x8276B8..0x82783C), then the tail. */
static int fan_run(EmFanOriginal *fan, EmFanOriginalPlayer *player, EmFanOriginalGlobals *g,
                   const EmFanOriginalWorkers *w, EmFanOriginalFault *fault)
{
    switch (fan->phase) {
    case 0:
        fan->timer = FAN_WAIT;
        fan->phase = (uint8_t)(fan->phase + 1);
        break;
    case 1:
        fan->timer = (int16_t)(fan->timer - 1);
        if (fan->timer != 0)
            break;
        fan->phase = (uint8_t)(fan->phase + 1);
        if (fan->flags2 != 0 || g->d810788 == 1)
            break;
        if (!w->w_001FBD50)
            return fan_fault(fault, 0x001FBD50u, EM_FAN_FAULT_NULL_WORKER);
        if (fan_leave(fault, 0x001FBD50u,
                      w->w_001FBD50(w->ctx, EM_FAN_ORIGINAL_SOUND_CUE, 0, FAN_SOUND_RANGE)) < 0)
            return -1;
        break;
    case 2:
        fan->spin = pose_add(fan->spin, FAN_STEP);
        if (fan->spin < FAN_SPIN_MAX)
            break;
        fan->phase = (uint8_t)(fan->phase + 1);
        fan->timer = FAN_HOLD;
        break;
    case 3:
        fan->timer = (int16_t)(fan->timer - 1);
        if (fan->timer != 0)
            break;
        fan->phase = (uint8_t)(fan->phase + 1);
        fan->rot_z = fan->flags2 == 0 ? -FAN_HOLD_END : FAN_HOLD_END;
        break;
    case 4:
        fan->spin = pose_sub(fan->spin, FAN_STEP);
        if (!(fan->spin <= 0.0f))
            break;
        fan->spin = 0.0f;
        fan->phase = 0;
        break;
    default:
        break;
    }

    /* 0x82783C: rot.z += spin (flags2 0) or -= spin, 001B1470, store. */
    fan->rot_z = fan->flags2 == 0 ? pose_add(fan->rot_z, fan->spin)
                                  : pose_sub(fan->rot_z, fan->spin);
    fan->rot_z = em_fan_original_wrap_001B1470(fan->rot_z);
    if (!w->w_001C6380)
        return fan_fault(fault, 0x001C6380u, EM_FAN_FAULT_NULL_WORKER);
    if (fan_leave(fault, 0x001C6380u, w->w_001C6380(w->ctx, fan)) < 0)
        return -1;

    /* 0x827880: flags2 == 1 and D_008106B8 == 0 reach the box. */
    if (fan->flags2 == 1 && g->d8106B8 == 0 && fan_box(fan, player, g, w, fault) < 0)
        return -1;

    /* 0x827ADC: jalr *(actor + 0x4C). */
    if (!w->w_draw_4C)
        return fan_fault(fault, EM_FAN_ORIGINAL_CALLBACK, EM_FAN_FAULT_NULL_WORKER);
    if (fan_leave(fault, EM_FAN_ORIGINAL_CALLBACK, w->w_draw_4C(w->ctx)) < 0)
        return -1;
    return 1;
}

int em_fan_original_tick(EmFanOriginal *fan, EmFanOriginalPlayer *player,
                         EmFanOriginalGlobals *globals, const EmFanOriginalWorkers *w,
                         EmFanOriginalFault *fault)
{
    if (!fault)
        return -1;
    if (fault->code != EM_FAN_FAULT_NONE)
        return -1;
    if (!fan || !w)
        return fan_fault(fault, EM_FAN_ORIGINAL_CALLBACK, EM_FAN_FAULT_NULL_WORKER);
    if (fan->freed)
        return fan_fault(fault, EM_FAN_ORIGINAL_CALLBACK, EM_FAN_FAULT_BAD_INDEX);

    switch (fan->lifecycle) {
    case 2:
    case 3: /* 0x827650 / 0x82765C -> 0x827AEC: 001AFC10(actor) */
        if (!w->w_001AFC10)
            return fan_fault(fault, 0x001AFC10u, EM_FAN_FAULT_NULL_WORKER);
        if (fan_leave(fault, 0x001AFC10u, w->w_001AFC10(w->ctx)) < 0)
            return -1;
        fan->freed = 1;
        return 0;
    case 1:
        if (!globals)
            return fan_fault(fault, EM_FAN_ORIGINAL_D_00810788, EM_FAN_FAULT_NULL_WORKER);
        return fan_run(fan, player, globals, w, fault);
    case 0: { /* 0x827680: 001B0FD0(actor); +0x38 = 0; +0xC8 = +/-pi/4 by +0x2E */
        if (!w->w_001B0FD0)
            return fan_fault(fault, 0x001B0FD0u, EM_FAN_FAULT_NULL_WORKER);
        int result = fan_leave(fault, 0x001B0FD0u, w->w_001B0FD0(w->ctx, fan));
        if (result < 0)
            return -1;
        if (result == 0)
            fan->lifecycle = (uint8_t)(fan->lifecycle + 1); /* 001B0FD0 */
        else if (result == 1)
            fan->lifecycle = 3; /* 001B0EA0 over the bone cap */
        else
            return fan_fault(fault, 0x001B0FD0u, EM_FAN_FAULT_BAD_RESULT);
        fan->spin = 0.0f;
        fan->rot_z = fan->flags2 == 0 ? FAN_QUARTER_PI : -FAN_QUARTER_PI;
        return 1;
    }
    default: /* lifecycle >= 4: returns without a call */
        return 1;
    }
}
