/* The panel's cell 18 and the panel / terminal indicator children's colour
 * step 001F54E0. The panel and the terminal draw themselves through
 * 001CAA00 over their records (em_area11_boxes_owner_draw; the level
 * smoke's check_owner_units); their children's +0x4C is 001CACB0 ->
 * 001CABA0 (em_indicator_bind_live_draw; tools/test_owner_draw_reference.py
 * part E). The children's behaviour and the terminal's level tail are
 * em_indicator_child (tests/indicator_child_test.c,
 * tools/test_census_unverified_reference.py); their node matrices (001C6380,
 * the terminal's 0x827E6C copy) are checked against the route captures by
 * the level smoke's check_indicator_children. */
#include <assert.h>
#include "../src/game/em_props.c"
#include "game/em_effect_kinds.h"

EmGameState g;
static int collision_published;

/* This fixture isolates owner lifetime; the separate collision tests run
 * the real loader, original face instructions and set2 query composition. */
int em_collision_cell_load(EmCollCell *cell, const char *path)
{ assert(strstr(path,"panel_cell18.emcb")); cell->uid=18;return 0; }
void em_collision_cell_free(EmCollCell *cell) { memset(cell,0,sizeof *cell); }
int em_collision_cell_bind(EmCollision *world, const EmCollCell *cell)
{ (void)world; assert(cell->uid==18);collision_published=1;return 1; }
void em_collision_cell_unbind(EmCollision *world, unsigned uid)
{ (void)world;(void)uid;collision_published=0; }

/* 001F54E0 with the RNG value `r` over `colour`: the child's new +0x80. */
static int w_rand(void *ctx, int32_t *v0) { *v0 = (int32_t)*(uint32_t *)ctx; return 0; }
static int w_draw(void *ctx, uint32_t fn, void *obj) { (void)ctx; (void)obj; return fn == 0x001CACB0u ? 0 : -1; }
static void c80_001F54E0(uint32_t r, const float colour[4], float c80[4])
{
    const EmEffectKindsWorkers w = {.ctx = &r, .w_00122BB8 = w_rand, .w_indirect = w_draw};
    EmEffectKinds k = {.workers = &w};
    memcpy(c80, colour, 4 * sizeof(float));
    assert(em_effect_kinds_001F54E0(&k, c80, c80, 0x001CACB0u, c80) == 0);
}

int main(void)
{
    float delta[4];
    const float red_panel[4]={1,0,0,1}, red_elevator[4]={1,0,0,.25f};
    /* Original frame4083: recovered SDK RNG inputs at ages12 and11. */
    c80_001F54E0(0x484cd471,red_panel,delta);
    assert(delta[0]==16.47052001953125f && delta[1]==-127);
    c80_001F54E0(0x31d71256,red_elevator,delta);
    assert(delta[0]==-7.024627685546875f && delta[2]==-127);

    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    assert(grate_install("scene")==0 && g.grate_present);
    grate_update();
    assert(collision_published);
    grate_unload(gfx);
    assert(!collision_published && !g.grate_present);
    puts("panel cell and the indicator colour step: PASS");
    return 0;
}
