#include "game/em_panel.h"
#include "game/em_ee_float.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <string.h>

#pragma STDC FP_CONTRACT OFF

void em_panel_init(EmPanel *panel, int completed)
{
    memset(panel,0,sizeof *panel);
    panel->status=completed ? 2 : 1;
    panel->phase=completed ? 3 : 0;
    panel->child_alive=!completed;
    panel->cost=2;
}

int em_panel_tick(EmPanel *panel, int has_small_battery,
                   const EmPanelHooks *hooks)
{
    if (!panel || !hooks || !hooks->align_player || !hooks->start_script ||
        !hooks->tick_script || !hooks->stop_indicator) return -1;
    void *context=hooks->context;
    switch (panel->phase) {
    case 0:
        if (!(panel->armed&4)) break;
        hooks->align_player(context);
        if (!has_small_battery) {
            hooks->start_script(context,EM_PANEL_NO_BATTERY,0x80000018u);
            hooks->tick_script(context);
            panel->status=2;
            panel->phase=4;
        } else if (panel->armed&1) {
            panel->charged=1;
            panel->status=2;
            panel->phase=1;
        } else {
            hooks->start_script(context,EM_PANEL_OPEN_BATTERY,0x80000018u);
            hooks->tick_script(context);
            panel->phase=5;
        }
        break;
    case 1:
    case 6: {
        int after_menu=panel->phase==6;
        if (!panel->charged) {
            panel->phase=4;
            hooks->start_script(context,EM_PANEL_CANCEL_SCRIPT,0);
        } else {
            panel->phase=2;
            hooks->start_script(context,after_menu ? EM_PANEL_POWER_AFTER_MENU :
                                                    EM_PANEL_POWER_SCRIPT,0);
            hooks->tick_script(context);
        }
        break;
    }
    case 2:
        if (hooks->tick_script(context)>0) {
            panel->armed=0;
            panel->status=2;
            panel->phase=3;
            if (panel->child_alive) {
                hooks->stop_indicator(context);
                panel->child_alive=0;
            }
        }
        break;
    case 4:
        if (hooks->tick_script(context)>0) {
            panel->status=1;
            panel->armed=0;
            panel->phase=0;
        }
        break;
    case 5:
        if (hooks->tick_script(context)>0) panel->phase=6;
        break;
    default:
        break;
    }
    return 0;
}

/* 001B1470 (COP1: em_ee_float.h, the measured EE model). */
static float panel_wrap(float value)
{
    const float pi=3.1415927410125732421875f;
    while (value>pi) value=em_ee_sub(value,2*pi);
    while (value<=-pi) value=em_ee_add(value,2*pi);
    return value;
}

int em_panel_candidate(const EmPanel *panel, const float owner[3],
                        float owner_yaw, const float player[3],
                        float player_yaw, float *distance)
{
    if (!panel || panel->status!=1 || panel->armed ||
        !isfinite(owner_yaw) || !isfinite(player_yaw)) return 0;
    /* 00183EF0's arithmetic is COP1 (em_ee_float.h, the measured EE model). */
    float dx=em_ee_sub(player[0],owner[0]);
    float dz=em_ee_sub(player[2],owner[2]);
    float xx=em_ee_mul(dx,dx);
    float zz=em_ee_mul(dz,dz);
    float squared=em_ee_add(xx,zz);
    /* 0011E748 calls the SDK's bit-by-bit0011CB90 square root, whose
     * final mantissa rounds to nearest/even. */
    float planar=sqrtf(squared);
    if (!(planar<=9.5f)) return 0;
    /* Original183EF0 publishes scratch3B98 before the later height and
     * facing gates. A subsequent candidate may consume this shared score
     * even when this panel is rejected. */
    if (distance) *distance=planar;
    float dy=em_ee_sub(player[1],owner[1]);
    if (!(sqrtf(em_ee_mul(dy,dy))<=20.0f)) return 0;
    float angle=em_ee_add(3.1415927410125732421875f,player_yaw);
    angle=panel_wrap(em_ee_sub(angle,owner_yaw));
    if (!(fabsf(angle)<=0.785398185253143310546875f)) return 0;
    return 1;
}

/* The one scalar owner for all 00157F60 model branches. AREA11's typed
 * panel wrapper and AREA01's borrowed record adapter consume these values. */
typedef struct {
    uint8_t request, parameter, status, charged, armed;
} PanelRequest;
static PanelRequest panel_request(uint8_t model, uint8_t cost)
{
    PanelRequest value = {1, (uint8_t)(0x80u + cost), 1, 0, 0};
    if (model == 0x38) { value.request = 6; value.parameter = 0x80; }
    else if (model == 0x37) { value.request = 5; value.parameter = 0x10; }
    else if (model == 0x2C) value.parameter = 0x40;
    return value;
}
uint8_t em_panel_battery_request(EmPanel *panel)
{
    PanelRequest value = panel_request(0x24, (uint8_t)panel->cost);
    panel->charged=value.charged;
    panel->armed=value.armed;
    panel->status=value.status;
    return value.parameter;
}

/* Preserve 00157F60's access order through the borrowed canonical views.
 * This only posts the original request; it never supplies a page or choice. */
int em_panel_request_00157F60(void *ctx, EmPanelMemory memory, uint32_t actor)
{
    if (!memory) return -1;
    uint8_t *p = memory(ctx, actor + 3u, 1, 0);
    if (!p) return -1;
    uint8_t model = *p, cost = 0;
    if (model != 0x38 && model != 0x37 && model != 0x2C) {
        p = memory(ctx, actor + 0x34u, 1, 0);
        if (!p) return -1;
        cost = *p;
    }
    PanelRequest value = panel_request(model, cost);
    p = memory(ctx, 0x008106B1u, 1, 1);
    if (!p) return -1;
    *p = value.parameter;
    p = memory(ctx, 0x008106B0u, 1, 1);
    if (!p) return -1;
    *p = value.request;
    p = memory(ctx, actor + 0x14u, 4, 0);
    if (!p) return -1;
    uint32_t self; memcpy(&self, p, 4);
    p = memory(ctx, 0x008106D0u, 4, 1);
    if (!p) return -1;
    memcpy(p, &self, 4);
    p = memory(ctx, actor + 0xAu, 1, 1);
    if (!p) return -1;
    *p = value.charged;
    p = memory(ctx, actor + 0xBu, 1, 1);
    if (!p) return -1;
    *p = value.armed;
    p = memory(ctx, actor, 1, 1);
    if (!p) return -1;
    *p = value.status;
    return 0;
}
