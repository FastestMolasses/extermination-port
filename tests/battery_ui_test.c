#include "game/em_battery_ui.h"
#include "game/em_hud.h"
#include <assert.h>
#include <stdio.h>

static int record,quads,uploads;
int em_gfx_overlay_texture_set(EmGfx *g,int slot,const uint8_t *p,uint32_t w,uint32_t h)
{(void)g;assert(slot==1 && p && w && h);uploads++;return 1;}
void em_gfx_overlay_canvas(EmGfx *g,float w,float h)
{(void)g;(void)w;(void)h;}
void em_hud_decor_invalidate(void) {}
void em_hud_background_sprite(EmGfx *g,float u,float v,float w,float h)
{(void)g;(void)u;(void)v;(void)w;(void)h;}
void em_hud_text(EmGfx *g,float x,float y,const char *s,EmHudTextStyle style)
{(void)g;(void)x;(void)y;(void)s;(void)style;}
void em_hud_text_color(EmGfx *g,float x,float y,const char *s,EmHudTextStyle style,uint32_t c)
{(void)g;(void)x;(void)y;(void)s;(void)style;(void)c;}
float em_hud_text_width(const char *s,EmHudTextStyle style)
{(void)s;(void)style;return 0;}
void em_gfx_overlay_sprite(EmGfx *g,float x,float y,float w,float h,
                            float u,float v,float u1,float v1,const float color[4])
{
    (void)g;quads++;
    if (record) printf("Q %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n",
                       x,y,w,h,u,v,u1,v1,color[0],color[1],color[2],color[3]);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    EmBatteryUI *ui=em_battery_ui_load(argv[1]);assert(ui);
    EmPanel panel;em_panel_init(&panel,0);
    int charge=12;
    assert(em_battery_ui_terminal_text(ui));
    assert(em_battery_ui_begin(ui,&panel,charge,0));
    assert(em_battery_ui_phase(ui)==EM_PANEL_MENU_CONFIRM);
    record=1;
    assert(em_battery_ui_render(ui,(EmGfx *)1,12,0));
    record=0;assert(quads==29 && uploads==1);
    assert(em_battery_ui_tick(ui,0x40,&charge,1)==EM_PANEL_MENU_CANCEL);
    assert(em_battery_ui_phase(ui)==EM_PANEL_MENU_BROWSE && charge==12);
    assert(em_battery_ui_tick(ui,0x20,&charge,1)==(EM_BATTERY_BACK_TO_STATUS|EM_PANEL_MENU_CANCEL));
    assert(em_battery_ui_tick(ui,0x40,&charge,0)==EM_BATTERY_NO_DEVICE_SOUND);
    for (int i=0;i<240;i++) assert(!em_battery_ui_tick(ui,0,&charge,0));
    assert(em_battery_ui_tick(ui,0x40,&charge,1)==EM_PANEL_MENU_ACCEPT);
    assert(em_battery_ui_tick(ui,0x8040,&charge,1)==(EM_PANEL_MENU_CURSOR|EM_PANEL_MENU_ACCEPT));
    int units=0,complete=0;
    for (int i=1;i<=61;i++) {
        unsigned events=em_battery_ui_tick(ui,0,&charge,1);
        if (events&EM_PANEL_MENU_UNIT_SOUND) {assert(i==1 || i==31);units++;}
        if (events&EM_PANEL_MENU_FINISHED) {assert(i==61);complete++;}
    }
    assert(units==2 && complete==1 && panel.charged && panel.armed==5 && charge==8);
    em_battery_ui_close(ui);
    assert(!em_battery_ui_render(ui,(EmGfx *)1,12,0));
    em_battery_ui_free(ui);
    puts("PASS battery UI load, default No, back, unavailable owner, reselect and real discharge");
}
