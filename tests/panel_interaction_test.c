#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "game/em_panel.h"

static int align_calls,script_ticks,indicator_stops,script_done;
static EmPanelScript last_script;
static uint32_t last_message;
static void align_player(void *context) { (void)context; ++align_calls; }
static void script_start(void *context,EmPanelScript script,uint32_t message)
{ (void)context; last_script=script; last_message=message; }
static int script_tick(void *context) { (void)context; ++script_ticks; return script_done; }
static void indicator_stop(void *context) { (void)context; ++indicator_stops; }

int main(void)
{
    const EmPanelHooks hooks={NULL,align_player,script_start,script_tick,indicator_stop};
    EmPanel panel;
    em_panel_init(&panel,0);
    assert(em_panel_tick(&panel,1,NULL)==-1);
    const float owner[3]={240,245,232.8f};
    float player[3]={240,230,224},distance;
    assert(em_panel_candidate(&panel,owner,-3.141592741f,player,0,&distance));
    assert(distance>8.7f && distance<8.9f);
    assert(!em_panel_candidate(&panel,owner,-3.141592741f,player,1,NULL));
    player[0]=250;
    assert(!em_panel_candidate(&panel,owner,-3.141592741f,player,0,NULL));
    player[0]=240; player[1]=225;
    assert(em_panel_candidate(&panel,owner,-3.141592741f,player,0,NULL));
    player[1]=nextafterf(225,-INFINITY);
    assert(!em_panel_candidate(&panel,owner,-3.141592741f,player,0,NULL));

    panel.armed=4;
    assert(em_panel_tick(&panel,0,&hooks)==0);
    assert(align_calls==1 && script_ticks==1 && last_script==EM_PANEL_NO_BATTERY);
    assert(last_message==0x80000018 && panel.phase==4 && panel.status==2);
    script_done=1;
    em_panel_tick(&panel,0,&hooks);
    assert(panel.phase==0 && panel.status==1 && !panel.armed && panel.child_alive);

    panel.armed=4; script_done=0;
    em_panel_tick(&panel,1,&hooks);
    assert(last_script==EM_PANEL_OPEN_BATTERY && panel.phase==5);
    assert(em_panel_battery_request(&panel)==0x82);
    assert(!panel.armed && !panel.charged && panel.status==1);
    script_done=1;
    em_panel_tick(&panel,1,&hooks);
    assert(panel.phase==6);
    /* Cancelled page follows the original timer10 cancel script; this
     * owner frame starts it but does not immediately execute its record. */
    int ticks=script_ticks;
    em_panel_tick(&panel,1,&hooks);
    assert(last_script==EM_PANEL_CANCEL_SCRIPT && panel.phase==4 && script_ticks==ticks);
    em_panel_tick(&panel,1,&hooks);
    assert(panel.phase==0 && panel.child_alive && !indicator_stops);

    EmPanelBatteryMenu menu;
    int charge=12;
    em_panel_battery_begin(&menu,charge);
    assert(em_panel_battery_step(&panel,&menu,0x40,&charge)==EM_PANEL_MENU_CANCEL);
    assert(menu.phase==EM_PANEL_MENU_BROWSE && charge==12 && !panel.charged);
    em_panel_battery_begin(&menu,charge);
    assert(em_panel_battery_step(&panel,&menu,0xa000,&charge)==EM_PANEL_MENU_CURSOR);
    assert(!menu.no_selected); /* Up takes precedence over Down */
    assert(em_panel_battery_step(&panel,&menu,0x40,&charge)==EM_PANEL_MENU_ACCEPT);
    assert(menu.phase==EM_PANEL_MENU_DISCHARGE && charge==12);
    assert(em_panel_battery_step(&panel,&menu,0,&charge)==EM_PANEL_MENU_UNIT_SOUND);
    assert(charge==10 && !panel.charged);
    for (int i=0;i<29;++i) assert(em_panel_battery_step(&panel,&menu,0,&charge)==0);
    assert(charge==10);
    assert(em_panel_battery_step(&panel,&menu,0,&charge)==EM_PANEL_MENU_UNIT_SOUND);
    assert(charge==8 && !panel.charged);
    for (int i=0;i<29;++i) assert(em_panel_battery_step(&panel,&menu,0,&charge)==0);
    assert(em_panel_battery_step(&panel,&menu,0,&charge)==EM_PANEL_MENU_FINISHED);
    assert(panel.charged && panel.armed==5 && charge==8);
    panel.phase=6; script_done=0;
    em_panel_tick(&panel,1,&hooks);
    assert(last_script==EM_PANEL_POWER_AFTER_MENU && panel.phase==2);
    assert(panel.child_alive && !indicator_stops);
    script_done=1;
    em_panel_tick(&panel,1,&hooks);
    assert(panel.phase==3 && panel.status==2 && !panel.child_alive && indicator_stops==1);

    em_panel_init(&panel,0); charge=2;
    em_panel_battery_begin(&menu,charge);
    em_panel_battery_step(&panel,&menu,0x8040,&charge);
    assert(menu.phase==EM_PANEL_MENU_INSUFFICIENT && charge==2);
    for (int i=0;i<239;++i) em_panel_battery_step(&panel,&menu,0,&charge);
    assert(menu.phase==EM_PANEL_MENU_INSUFFICIENT);
    em_panel_battery_step(&panel,&menu,0,&charge);
    assert(menu.phase==EM_PANEL_MENU_BROWSE && !panel.charged);
    charge=12;
    em_panel_battery_begin(&menu,charge);
    em_panel_battery_step(&panel,&menu,0x8040,&charge);
    unsigned events=em_panel_battery_step(&panel,&menu,0x10,&charge);
    assert(events==(EM_PANEL_MENU_UNIT_SOUND|EM_PANEL_MENU_ACCEPT|EM_PANEL_MENU_FINISHED));
    assert(charge==8 && panel.charged && panel.armed==5);
    em_panel_init(&panel,1);
    assert(panel.phase==3 && !panel.child_alive);
    puts("original panel gates, owner/script handshakes and BATTERY discharge: PASS");
}
