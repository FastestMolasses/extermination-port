/* Original BATTERY page002149F0, restricted to the type24 panel request.
 * The status dispatcher still owns entering/leaving the page. This module
 * never converts No/Back into a successful interaction. */
#ifndef EM_BATTERY_UI_H
#define EM_BATTERY_UI_H
#include "em_gfx.h"
#include "game/em_panel.h"

typedef struct EmBatteryUI EmBatteryUI;
enum {
    EM_BATTERY_BACK_TO_STATUS = 32,
    EM_BATTERY_NO_DEVICE_SOUND = 64 /* original0020CD80 -> sound2 */
};

EmBatteryUI *em_battery_ui_load(const char *path);
void em_battery_ui_free(EmBatteryUI *ui);
/* kind0/1/2 selects the highest available original battery item1B/1C/1D.
 * Returns0 for missing data/owner. Does not acquire the status dispatcher. */
int em_battery_ui_begin(EmBatteryUI *ui, EmPanel *owner, int charge, int kind);
/* buttons are original D810E74. Returns EM_PANEL_MENU_* sound/finish
 * events, NO_DEVICE_SOUND and BACK_TO_STATUS. owner_available is the
 * original00185420 lookup result when reselecting the battery row.
 * The caller persists charge through the
 * inventory API and routes actual status transitions. */
unsigned em_battery_ui_tick(EmBatteryUI *ui, unsigned buttons, int *charge, int owner_available);
int em_battery_ui_render(EmBatteryUI *ui, EmGfx *gfx, int capacity, unsigned held_buttons);
void em_battery_ui_close(EmBatteryUI *ui);
EmPanelMenuPhase em_battery_ui_phase(const EmBatteryUI *ui);
const char *em_battery_ui_terminal_text(const EmBatteryUI *ui);
#endif
