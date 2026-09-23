#include <assert.h>
#include "../src/game/em_props.c"

EmGameState g;
static int draws, random_calls, powered;
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

uint32_t em_random_next(void) { ++random_calls; return 0x40000000; }
/* D_0081084C bit 7 (the canonical progress byte in the game). */
int em_game_terminal_powered(void) { return powered; }
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
    float delta[3];
    const float red_panel[4]={1,0,0,1}, red_elevator[4]={1,0,0,.25f};
    /* Original frame4083: recovered SDK RNG inputs at ages12 and11. */
    em_effect_delta(0x484cd471,red_panel,delta);
    assert(delta[0]==16.47052001953125f && delta[1]==-127);
    em_effect_delta(0x31d71256,red_elevator,delta);
    assert(delta[0]==-7.024627685546875f && delta[2]==-127);

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
    em_props_indicators_tick();
    em_props_indicators_draw(gfx,vp);
    assert(draws==0 && random_calls==0);
    em_props_indicators_tick();
    em_props_indicators_draw(gfx,vp);
    assert(draws==2 && random_calls==2 && last_tint[0]==1);
    assert(last_palette[12]==224 && last_palette[13]==230);
    powered=1;
    for (int i=0;i<16;++i) {
        grate_update();
        em_props_indicators_tick();
    }
    assert(indicators[1].level==128 && indicators[1].tint[1]==1);
    assert(indicators[0].visible); /* only its own script completion stops it */
    assert(g.grate_palette[12]==240 && g.grate_palette[13]==245);
    em_props_panel_complete();
    assert(!indicators[0].visible);
    assert(collision_published);
    powered=0;
    for (int i=0;i<16;++i) em_props_indicators_tick();
    assert(indicators[1].level==0 && indicators[1].tint[0]==1);

    /* The ride (00828050's carry) moved to the AREA11 interaction host in
     * WP-4 (tools/test_elevator_reference.py checks it against route 04);
     * the legacy elevator_tick this test drove is gone. */
    elevator_unload(gfx);
    grate_unload(gfx);
    assert(!collision_published);
    assert(!indicators[0].mesh && !indicators[1].mesh);
    int previous=draws;
    em_props_indicators_tick();
    em_props_indicators_draw(gfx,vp);
    assert(draws==previous);
    puts("original prop indicators and fixed panel: PASS");
    return 0;
}
