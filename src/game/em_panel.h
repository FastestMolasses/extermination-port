/* Original type24 power-panel controller (00159210 / 00157860).
 * This core is deliberately not registered in the live scene yet: its
 * script, camera, player animation and BATTERY page need real bindings.
 * A missing script/UI binding must never be treated as success. */
#ifndef EM_PANEL_H
#define EM_PANEL_H

#include <stdint.h>

typedef enum {
    EM_PANEL_NO_BATTERY = 0x00246F20,
    EM_PANEL_OPEN_BATTERY = 0x002477A0,
    EM_PANEL_POWER_SCRIPT = 0x00247BA0,
    EM_PANEL_POWER_AFTER_MENU = 0x00247BE0,
    EM_PANEL_CANCEL_SCRIPT = 0x00247DA0
} EmPanelScript;

typedef struct {
    uint8_t status;       /* actor+0: 1 scan eligible, 2 inactive */
    uint8_t phase;        /* actor+5; original substate values */
    uint8_t charged;      /* actor+A; set by successful menu discharge */
    uint8_t armed;        /* actor+B; 4 use scan, 5 discharged */
    uint8_t child_alive;
    uint16_t cost;        /* actor+34: full units, type24 uses 2 */
} EmPanel;

typedef struct {
    void *context;
    /* 001B6F00: yaw=wrap(ownerYaw+pi), position=ownerWorld*(.3,0,9,1),
     * retaining player ground Y. The host must shift all player position
     * mirrors through its original alignment binding (00182F90). */
    void (*align_player)(void *context);
    void (*start_script)(void *context, EmPanelScript script,
                          uint32_t message_token);
    int (*tick_script)(void *context); /* positive only when actually done */
    void (*stop_indicator)(void *context);
} EmPanelHooks;

void em_panel_init(EmPanel *panel, int completed);
/* Returns -1 if required host bindings are absent. Otherwise performs
 * exactly one owner tick; script_start and immediate script_tick calls
 * preserve the original order. has_small_battery is item-count1B != 0. */
int em_panel_tick(EmPanel *panel, int has_small_battery,
                   const EmPanelHooks *hooks);

/* Per-object gate of00183EF0 for this class4/mode0/type24 actor. The
 * caller owns global00184BA0 gates, CROSS edge, and nearest-winner
 * arbitration. This does not arm the panel or consume a button. */
int em_panel_candidate(const EmPanel *panel, const float owner[3],
                        float owner_yaw, const float player[3],
                        float player_yaw, float *distance);

/* Original00157F60 callback for type24: clear actor+A/B, restore status1,
 * request ordinary BATTERY page B0=1, B1=0x80+cost, owner pointer B/D0.
 * Returns B1; the UI owner keeps the panel association itself. */
uint8_t em_panel_battery_request(EmPanel *panel);

typedef enum {
    EM_PANEL_MENU_BROWSE,
    EM_PANEL_MENU_CONFIRM,
    EM_PANEL_MENU_INSUFFICIENT,
    EM_PANEL_MENU_DISCHARGE,
    EM_PANEL_MENU_COMPLETE
} EmPanelMenuPhase;

typedef struct {
    EmPanelMenuPhase phase;
    uint8_t no_selected;  /* original page+6, initially1 */
    int initial_charge;  /* page+12; internal half-units */
    int timer;
} EmPanelBatteryMenu;

enum {
    EM_PANEL_MENU_CURSOR = 1,
    EM_PANEL_MENU_ACCEPT = 2,
    EM_PANEL_MENU_CANCEL = 4,
    EM_PANEL_MENU_UNIT_SOUND = 8,
    EM_PANEL_MENU_FINISHED = 16
};

/* The type24 branch of002149F0 states4/5/6, after its real page has
 * opened. Button bits are original D00810E74 edges/repeats. This owns
 * neither drawing nor page transitions. Returning BROWSE means control
 * returns to the battery list; it does not silently close the menu. */
void em_panel_battery_begin(EmPanelBatteryMenu *menu, int charge);
unsigned em_panel_battery_step(EmPanel *panel, EmPanelBatteryMenu *menu,
                                unsigned buttons, int *charge);

#endif
