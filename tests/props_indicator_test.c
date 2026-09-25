/* The panel / terminal indicator draws (the children's +0x4C 001CACB0 at
 * the owner's matrix, with 001F54E0's colour), the fixed panel and its cell.
 * The children's behaviour and the terminal's level tail are
 * em_indicator_child (tests/indicator_child_test.c,
 * tools/test_census_unverified_reference.py). */
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
void em_model_palette_at(const EmModel *m, uint32_t clip, double time, float *out)
{
    (void)clip; (void)time;
    memset(out,0,m->bone_count*16*sizeof(float));
    for (uint32_t i=0;i<m->bone_count;++i)
        for (unsigned j=0;j<4;++j) out[i*16+j*5]=1;
}
void palette_apply_placement(float *out, uint32_t count, const float pos[3], float yaw)
{
    for (uint32_t i=0;i<count;++i) {
        float *m=out+i*16;
        m[0]=cosf(yaw); m[2]=-sinf(yaw);
        m[8]=sinf(yaw); m[10]=cosf(yaw);
        memcpy(m+12,pos,3*sizeof(float));
    }
}
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
    const float panel[3]={240,245,232.8f}, vp[16]={0};
    assert(grate_install(gfx,"scene","panel",panel,-3.14159274f)==0);
    grate_update();assert(collision_published);
    assert(em_props_indicator_install(gfx,"scene","panel","red")==0);
    em_model_load(&g.elev_model,"elevator");
    g.elev_mesh=(EmGfxMesh *)(uintptr_t)2;
    g.elev_palette=calloc(32,sizeof(float));
    g.elev_has_mesh=1;
    g.elev_pos[0]=224; g.elev_pos[1]=230; g.elev_pos[2]=250.7f;
    elevator_pose();
    assert(em_props_indicator_install(gfx,"scene","elevator","red")==0);
    em_props_indicators_draw(gfx,vp);
    assert(draws==0);                          /* nothing submitted */
    float c80[4];
    c80_001F54E0(0x40000000,red_elevator,c80);
    assert(em_props_indicator_submit(1,c80)==0);
    assert(em_props_indicator_submit(2,c80)==-1);
    em_props_indicators_draw(gfx,vp);
    assert(draws==1 && last_tint[0]==1 && last_tint[1]==1.0f/128);
    assert(last_palette[12]==224 && last_palette[13]==230);
    em_props_indicators_draw(gfx,vp);
    assert(draws==1);                          /* one draw per 001F54E0 */
    const float green_elevator[4]={0,1,0,.25f};
    c80_001F54E0(0x40000000,green_elevator,c80);
    assert(em_props_indicator_submit(1,c80)==0);
    c80_001F54E0(0x40000000,red_panel,c80);
    assert(em_props_indicator_submit(0,c80)==0);
    grate_update();
    em_props_indicators_draw(gfx,vp);
    assert(draws==3 && last_tint[0]==1.0f/128 && last_tint[1]==1);   /* slot 1 last */
    assert(g.grate_palette[12]==240 && g.grate_palette[13]==245);
    assert(collision_published);

    /* The ride (00828050's carry) moved to the AREA11 interaction host in
     * WP-4 (tools/test_elevator_reference.py checks it against route 04);
     * the legacy elevator_tick this test drove is gone. */
    elevator_unload(gfx);
    grate_unload(gfx);
    assert(!collision_published);
    assert(!indicators[0].mesh && !indicators[1].mesh);
    int previous=draws;
    assert(em_props_indicator_submit(0,c80)==-1);   /* no mesh after the unload */
    em_props_indicators_draw(gfx,vp);
    assert(draws==previous);
    puts("original prop indicators and fixed panel: PASS");
    return 0;
}
