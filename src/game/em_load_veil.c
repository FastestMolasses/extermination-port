/* Area-load veil state machine (S12a). See em_load_veil.h. */
#include "game/em_load_veil.h"

#include <stddef.h>

#include "game/em_pose_math.h"

#pragma STDC FP_CONTRACT OFF

static int call(int result, uint32_t address, uint32_t *fault_address)
{
    if (result < 0 && fault_address)
        *fault_address = address;
    return result < 0 ? -1 : 0;
}

static int d2830(const EmLoadVeilWorkers *w, int group, int enable, uint32_t *fault_address)
{
    if (!w || !w->w_001D2830)
        return call(-1, 0x001D2830u, fault_address);
    return call(w->w_001D2830(w->ctx, group, enable), 0x001D2830u, fault_address);
}

static int particles(EmLoadVeil *v, const EmLoadVeilWorkers *w, uint32_t *fault_address)
{
    if (!w || !w->w_0021B1B0)
        return call(-1, 0x0021B1B0u, fault_address);
    if (call(w->w_0021B1B0(w->ctx, v), 0x0021B1B0u, fault_address) < 0)
        return -1;
    if (!w->w_0021B500)
        return call(-1, 0x0021B500u, fault_address);
    return call(w->w_0021B500(w->ctx, v), 0x0021B500u, fault_address);
}

/* EE add.s / mul.s: the exact result truncated toward zero. */
static float ee_add(float a, float b) { return pose_scalar((double)a + (double)b); }
static float ee_mul(float a, float b) { return pose_scalar((double)a * (double)b); }

int em_load_veil_0021B180(EmLoadVeil *v, const EmLoadVeilWorkers *w, uint32_t *fault_address)
{
    v->b03 = 0;
    v->sub2 = 0;
    v->sub = 0;
    v->state = 0;
    v->w04 = 0;
    v->w18 = 0x8000u;
    return d2830(w, 0x20, 0, fault_address);
}

void em_load_veil_0021B840(EmLoadVeil *v)
{
    v->state = 2;
    v->b03 = 0;
    v->sub2 = 0;
    v->sub = 0;
}

int em_load_veil_0021B550(EmLoadVeil *v, const EmLoadVeilWorkers *w, uint32_t *fault_address)
{
    static const float ramp[3] = {0.01f, 0.008f, 0.006f};
    static const float decay[3] = {0.9f, 0.92f, 0.94f};
    switch (v->state) {
    case 0:
        if (v->sub == 0 || v->sub == 1) {
            if (d2830(w, 2, 0, fault_address) < 0 || d2830(w, 3, 1, fault_address) < 0)
                return -1;
            v->sub = (uint8_t)(v->sub + 1);
        } else {
            v->level[2] = 0.0f;
            v->level[1] = 0.0f;
            v->level[0] = 0.0f;
            v->state = 1;
        }
        return 0;
    case 1:
        if (d2830(w, 3, 1, fault_address) < 0)
            return -1;
        for (int i = 0; i < 3; ++i) {
            float f = ee_add(ramp[i], v->level[i]);
            v->level[i] = f < 1.0f ? f : 1.0f;
        }
        return particles(v, w, fault_address) < 0 ? -1 : 0;
    case 2:
        if (v->sub == 0) {
            for (int i = 0; i < 3; ++i)
                v->level[i] = ee_mul(v->level[i], decay[i]);
            if (d2830(w, 3, 1, fault_address) < 0 || particles(v, w, fault_address) < 0)
                return -1;
            if (v->level[0] < 0.01f && v->level[1] < 0.01f && v->level[2] < 0.01f)
                v->sub = 1;
        } else if (v->sub == 1) {
            if (d2830(w, 2, 0, fault_address) < 0)
                return -1;
            if (v->sub2 <= 2) {
                if (d2830(w, 3, 1, fault_address) < 0)
                    return -1;
                v->sub2 = (uint8_t)(v->sub2 + 1);
            } else {
                v->state = 3;
            }
        }
        return 0;
    default: /* state 3 and every other value */
        if (d2830(w, 2, 0, fault_address) < 0)
            return -1;
        return 1;
    }
}
