/* The panel / terminal indicator draws (the children's +0x4C 001CACB0 stand-in
 * at the child's own node 0, with 001F54E0's colour) and the panel's cell 18.
 * The panel and the terminal draw themselves through 001CAA00 over their
 * records (em_area11_boxes_owner_draw; the level smoke's check_owner_units).
 * The children's behaviour and the terminal's level tail are
 * em_indicator_child (tests/indicator_child_test.c,
 * tools/test_census_unverified_reference.py); their node matrices (001C6380,
 * the terminal's 0x827E6C copy) are checked against the route captures by
 * the level smoke's check_indicator_children. */
#include <assert.h>
#include "../src/game/em_props.c"
#include "game/em_effect_kinds.h"

EmGameState g;
static int draws, random_calls;
static float last_palette[32], last_tint[4];
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
static int w_rand(void *ctx, int32_t *v0) { ++random_calls; *v0 = (int32_t)*(uint32_t *)ctx; return 0; }
static int w_draw(void *ctx, uint32_t fn, void *obj) { (void)ctx; (void)obj; return fn == 0x001CACB0u ? 0 : -1; }
static void c80_001F54E0(uint32_t r, const float colour[4], float c80[4])
{
    const EmEffectKindsWorkers w = {.ctx = &r, .w_00122BB8 = w_rand, .w_indirect = w_draw};
    EmEffectKinds k = {.workers = &w};
    memcpy(c80, colour, 4 * sizeof(float));
    assert(em_effect_kinds_001F54E0(&k, c80, c80, 0x001CACB0u, c80) == 0);
}
int em_model_load(EmModel *m, const char *path)
{ (void)path; memset(m,0,sizeof *m); m->bone_count=2; return 0; }
void em_model_free(EmModel *m) { memset(m,0,sizeof *m); }
EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *verts, uint32_t count,
    const uint32_t *indices, uint32_t index_count, const EmGfxTexDesc *texs,
    uint32_t tex_count, const uint8_t *texels, uint32_t flags)
{
    (void)gfx; (void)verts; (void)count; (void)indices; (void)index_count;
    (void)texs; (void)tex_count; (void)texels; (void)flags;
    return (EmGfxMesh *)(uintptr_t)2;
}
void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh)
{ (void)gfx; (void)mesh; }
void em_gfx_draw_skinned_additive(EmGfx *gfx, EmGfxMesh *mesh,
    const float *vp, const float *palette, uint32_t count, const float rgba[4])
{
    (void)gfx; (void)mesh; (void)vp;
    assert(count==2);
    ++draws;
    memcpy(last_palette,palette,sizeof last_palette);
    memcpy(last_tint,rgba,sizeof last_tint);
}

/* A node matrix in the original row layout (row 3 the translation). */
static void node_at(float m[16], float x, float y, float z, float c, float sn)
{
    memset(m,0,16*sizeof(float));
    m[0]=c; m[2]=-sn; m[5]=1; m[8]=sn; m[10]=c; m[15]=1;
    m[12]=x; m[13]=y; m[14]=z;
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
    random_calls=0;

    EmGfx *gfx=(EmGfx *)(uintptr_t)1;
    const float vp[16]={0};
    assert(grate_install("scene")==0 && g.grate_present);
    grate_update();assert(collision_published);
    assert(em_props_indicator_install(gfx,"scene","panel","red")==0);
    assert(em_props_indicator_install(gfx,"scene","elevator","red")==0);
    assert(em_props_indicator_install(gfx,"scene","lamp","red")==-1);
    em_props_indicators_draw(gfx,vp);
    assert(draws==0);                          /* nothing submitted */
    float c80[4], upper[16], lower[16], panel[16];
    node_at(upper,224,230,250.7f,1,0);
    node_at(lower,224,190,250.7f,1,0);
    node_at(panel,240,245,232.8f,-1,0);
    c80_001F54E0(0x40000000,red_elevator,c80);
    assert(em_props_indicator_submit(1,c80,upper)==0);
    assert(em_props_indicator_submit(2,c80,upper)==-1);
    assert(em_props_indicator_submit(1,c80,NULL)==-1);
    em_props_indicators_draw(gfx,vp);
    assert(draws==1 && last_tint[0]==1 && last_tint[1]==1.0f/128);
    /* Every bone of the model takes the child's node 0, unchanged. */
    assert(memcmp(last_palette,upper,sizeof upper)==0 && memcmp(last_palette+16,upper,sizeof upper)==0);
    em_props_indicators_draw(gfx,vp);
    assert(draws==1);                          /* one draw per 001F54E0 */
    /* The terminal's child after the ride: its slot holds the terminal's
     * node at the lower floor (0x827E6C). */
    const float green_elevator[4]={0,1,0,.25f};
    c80_001F54E0(0x40000000,green_elevator,c80);
    assert(em_props_indicator_submit(1,c80,lower)==0);
    c80_001F54E0(0x40000000,red_panel,c80);
    assert(em_props_indicator_submit(0,c80,panel)==0);
    grate_update();
    em_props_indicators_draw(gfx,vp);
    assert(draws==3 && last_tint[0]==1.0f/128 && last_tint[1]==1);   /* slot 1 last */
    assert(memcmp(last_palette,lower,sizeof lower)==0);
    assert(collision_published);

    grate_unload(gfx);
    assert(!collision_published && !g.grate_present);
    assert(!indicators[0].mesh && !indicators[1].mesh);
    int previous=draws;
    assert(em_props_indicator_submit(0,c80,panel)==-1);   /* no mesh after the unload */
    em_props_indicators_draw(gfx,vp);
    assert(draws==previous);
    puts("original prop indicators and the panel's cell: PASS");
    return 0;
}
