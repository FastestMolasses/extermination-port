#include "game/em_panel.h"
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

static float panel_wrap(float value)
{
    const float pi=3.1415927410125732421875f;
    while (value>pi) value=em_effect_float32((double)value-2*pi);
    while (value<=-pi) value=em_effect_float32((double)value+2*pi);
    return value;
}

int em_panel_candidate(const EmPanel *panel, const float owner[3],
                        float owner_yaw, const float player[3],
                        float player_yaw, float *distance)
{
    if (!panel || panel->status!=1 || panel->armed ||
        !isfinite(owner_yaw) || !isfinite(player_yaw)) return 0;
    float dx=em_effect_float32((double)player[0]-owner[0]);
    float dz=em_effect_float32((double)player[2]-owner[2]);
    float xx=em_effect_float32((double)dx*dx);
    float zz=em_effect_float32((double)dz*dz);
    float squared=em_effect_float32((double)xx+zz);
    /* 0011E748 calls the SDK's bit-by-bit0011CB90 square root, whose
     * final mantissa rounds to nearest/even. Its argument arithmetic
     * above still uses the EE's separate truncating float operations. */
    float planar=sqrtf(squared);
    if (!(planar<=9.5f)) return 0;
    /* Original183EF0 publishes scratch3B98 before the later height and
     * facing gates. A subsequent candidate may consume this shared score
     * even when this panel is rejected. */
    if (distance) *distance=planar;
    float dy=em_effect_float32((double)player[1]-owner[1]);
    if (!(sqrtf(em_effect_float32((double)dy*dy))<=20.0f)) return 0;
    float angle=em_effect_float32(3.1415927410125732421875+(double)player_yaw);
    angle=panel_wrap(em_effect_float32((double)angle-owner_yaw));
    if (!(fabsf(angle)<=0.785398185253143310546875f)) return 0;
    return 1;
}

uint8_t em_panel_battery_request(EmPanel *panel)
{
    panel->charged=0;
    panel->armed=0;
    panel->status=1;
    return (uint8_t)(0x80+panel->cost);
}

void em_panel_battery_begin(EmPanelBatteryMenu *menu, int charge)
{
    *menu=(EmPanelBatteryMenu){.phase=EM_PANEL_MENU_CONFIRM,
                               .no_selected=1,.initial_charge=charge};
}

unsigned em_panel_battery_step(EmPanel *panel, EmPanelBatteryMenu *menu,
                                unsigned buttons, int *charge)
{
    unsigned events=0;
    switch (menu->phase) {
    case EM_PANEL_MENU_CONFIRM:
        if (buttons&0x8000) {
            if (menu->no_selected) {
                --menu->no_selected;
                events|=EM_PANEL_MENU_CURSOR;
            }
        } else if (buttons&0x2000) {
            if (!menu->no_selected) {
                ++menu->no_selected;
                events|=EM_PANEL_MENU_CURSOR;
            }
        }
        if (buttons&0x40) {
            if (menu->no_selected) {
                menu->phase=EM_PANEL_MENU_BROWSE;
                return events|EM_PANEL_MENU_CANCEL;
            }
            if (*charge<2*panel->cost) {
                menu->phase=EM_PANEL_MENU_INSUFFICIENT;
                menu->timer=240;
                return events|EM_PANEL_MENU_CANCEL;
            }
            menu->phase=EM_PANEL_MENU_DISCHARGE;
            menu->timer=1;
            return events|EM_PANEL_MENU_ACCEPT;
        }
        if (buttons&0x20) {
            menu->phase=EM_PANEL_MENU_BROWSE;
            events|=EM_PANEL_MENU_CANCEL;
        }
        break;
    case EM_PANEL_MENU_INSUFFICIENT:
        if (buttons&0x60) {
            menu->phase=EM_PANEL_MENU_BROWSE;
            events|=EM_PANEL_MENU_CANCEL;
        } else if (--menu->timer==0) menu->phase=EM_PANEL_MENU_BROWSE;
        break;
    case EM_PANEL_MENU_DISCHARGE: {
        int target=menu->initial_charge-2*panel->cost;
        if (--menu->timer==0) {
            menu->timer=30;
            if (*charge==target) goto complete;
            *charge-=2;
            events|=EM_PANEL_MENU_UNIT_SOUND;
        }
        if (buttons&0x870) {
            *charge=target;
            events|=EM_PANEL_MENU_ACCEPT;
            goto complete;
        }
        break;
    complete:
        panel->charged=1;
        panel->armed=5;
        menu->phase=EM_PANEL_MENU_COMPLETE;
        events|=EM_PANEL_MENU_FINISHED;
        break;
    }
    default:
        break;
    }
    return events;
}
