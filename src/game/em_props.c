/* Original AREA11 elevator/switch models and their indicator children.
 * Source mapping and validation: docs/OPENING_SCENERY.md. The historical
 * grate_* names survive only at the scene boundary; no guessed gate
 * motion or synthetic collision box is attached to that switch model. */

#include "game/em_props.h"

#include "game/em_game_internal.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"

typedef struct {
    EmModel model;
    EmGfxMesh *mesh;
    float *palette;
    int enabled, initialized, visible, level;
    float tint[4];
} PropIndicator;

static PropIndicator indicators[2]; /* model75 panel; model10 elevator */

static void indicator_unload(EmGfx *gfx, PropIndicator *indicator)
{
    if (indicator->mesh) em_gfx_mesh_destroy(gfx,indicator->mesh);
    em_model_free(&indicator->model);
    free(indicator->palette);
    memset(indicator,0,sizeof *indicator);
}

/* 00827B10 owns per-area model0F. Script helper00828050 moves this
 * same actor; its model10 child copies the parent's node0 world matrix. */
static int elevator_lower;

void elevator_pose(void)
{
    if (!g.elev_has_mesh || !g.elev_palette) return;
    em_model_palette_at(&g.elev_model,0,0.0,g.elev_palette);
    palette_apply_placement(g.elev_palette,g.elev_model.bone_count,
                            g.elev_pos,g.elev_yaw);
}

/* Original callback00828050 phase0: choose direction from81083A, play
 * cue452/453, and return without moving during this initialization tick.
 * Legacy function name retained for the existing interaction boundary. */
void elevator_descent_begin(void)
{
    g.elev_pending=0;
    g.elev_state=1;
    g.elev_frame=-1;
    g.elev_rate=elevator_lower ? 0.26666668f : -0.26666668f;
    em_sfx_play_at(elevator_lower ? 0x452u : 0x453u,
                   g.elev_has_mesh ? g.elev_pos : g.pos,300.0f);
}

void elevator_tick(void)
{
    if (g.elev_pending && !g.interact_active && g.elev_state==0)
        elevator_descent_begin();
    if (g.elev_state!=1) {
        elevator_pose();
        return;
    }
    if (g.elev_frame<0) {
        g.elev_frame=0;
        return;
    }
    /* Original EE add.s truncates each new Y independently. The camera
     * target follows player Y in the host camera update. */
    g.pos[1]=em_effect_float32((double)g.pos[1]+g.elev_rate);
    g.elev_pos[1]=em_effect_float32((double)g.elev_pos[1]+g.elev_rate);
    if (++g.elev_frame>=150) {
        /* The owning827B10 script completes: toggle81083A and snap its
         * own Y exactly. The original supports another ride in reverse. */
        elevator_lower=!elevator_lower;
        g.elev_pos[1]=elevator_lower ? 190.0f : 230.0f;
        g.elev_state=0;
    }
    elevator_pose();
}

void elevator_unload(EmGfx *gfx)
{
    indicator_unload(gfx,&indicators[1]);
    if (g.elev_mesh) em_gfx_mesh_destroy(gfx,g.elev_mesh);
    em_model_free(&g.elev_model);
    free(g.elev_palette);
    g.elev_mesh=NULL;
    g.elev_palette=NULL;
    g.elev_has_mesh=0;
    g.elev_state=0;
    g.elev_pending=0;
    elevator_lower=0;
}

/* The historical grate_* API names now refer to the actual model04
 * switch actor (00159210, AREA11 placement18). It has a static model and
 * a separate model75 indicator. The old world-X slide and invented wide
 * blocker had no support in that callback or the original model. */
static void grate_pose(void)
{
    if (!g.grate_present || !g.grate_palette) return;
    em_model_palette_at(&g.grate_model, 0, 0.0, g.grate_palette);
    palette_apply_placement(g.grate_palette, g.grate_model.bone_count,
                            g.grate_pos, g.grate_yaw);
}

int grate_install(EmGfx *gfx, const char *scene_dir,
                   const char *name, const float pos[3], float yaw)
{
    grate_unload(gfx);
    memcpy(g.grate_pos,pos,sizeof g.grate_pos);
    g.grate_yaw=yaw;
    char path[1024];
    snprintf(path,sizeof path,"%s/%s",scene_dir,name);
    if (em_model_load(&g.grate_model,path)!=0) return -1;
    g.grate_present=1;
    g.grate_mesh=em_gfx_mesh_create(gfx,g.grate_model.verts,
        g.grate_model.vert_count,g.grate_model.indices,g.grate_model.index_count,
        (const EmGfxTexDesc *)g.grate_model.texs,g.grate_model.tex_count,
        g.grate_model.texels,g.grate_model.flags);
    g.grate_palette=malloc(g.grate_model.bone_count*16*sizeof(float));
    if (!g.grate_mesh || !g.grate_palette) {
        grate_unload(gfx);
        return -1;
    }
    grate_pose();
    printf("manifest: original AREA11 model04 panel at (%.1f, %.1f, %.1f)\n",
           pos[0],pos[1],pos[2]);
    return 0;
}

void grate_update(void)
{
    grate_pose();
}

void grate_unload(EmGfx *gfx)
{
    indicator_unload(gfx,&indicators[0]);
    if (g.grate_mesh) em_gfx_mesh_destroy(gfx,g.grate_mesh);
    em_model_free(&g.grate_model);
    free(g.grate_palette);
    g.grate_mesh=NULL;
    g.grate_palette=NULL;
    g.grate_present=0;
}

int em_props_indicator_install(EmGfx *gfx, const char *scene_dir,
                                const char *kind, const char *file)
{
    if (!kind || !gfx || !scene_dir || !file) return -1;
    int slot=strcmp(kind,"panel")==0 ? 0 : strcmp(kind,"elevator")==0 ? 1 : -1;
    if (slot<0 || !(slot==0 ? g.grate_present : g.elev_has_mesh)) return -1;
    PropIndicator *indicator=&indicators[slot];
    indicator_unload(gfx,indicator);
    char path[1024];
    snprintf(path,sizeof path,"%s/%s",scene_dir,file);
    if (em_model_load(&indicator->model,path)!=0) return -1;
    if (!indicator->model.bone_count || indicator->model.bone_count>8) {
        indicator_unload(gfx,indicator);
        return -1;
    }
    indicator->mesh=em_gfx_mesh_create(gfx,indicator->model.verts,
        indicator->model.vert_count,indicator->model.indices,
        indicator->model.index_count,(const EmGfxTexDesc *)indicator->model.texs,
        indicator->model.tex_count,indicator->model.texels,indicator->model.flags);
    indicator->palette=malloc(indicator->model.bone_count*16*sizeof(float));
    if (!indicator->mesh || !indicator->palette) {
        indicator_unload(gfx,indicator);
        return -1;
    }
    /* 00159210 omits its red child when its completed bit is already set
     * at initialization. 00827B10 always allocates the elevator child. */
    indicator->enabled=slot==1 || !em_game_terminal_powered();
    return 0;
}

void em_props_panel_complete(void)
{
    /* 00159210 state1/sub2, after its own script finishes: child[4]=3.
     * Called only by that interaction's completion, not by a global power
     * toggle. The panel geometry remains at its original placement. */
    indicators[0].enabled=0;
    indicators[0].visible=0;
}

void em_props_indicators_tick(void)
{
    for (int slot=0;slot<2;++slot) {
        PropIndicator *indicator=&indicators[slot];
        indicator->visible=0;
        if (!indicator->mesh || !indicator->enabled) continue;
        if (!indicator->initialized) {
            indicator->initialized=1;
            continue;
        }
        float color[4]={1,0,0,slot==0 ? 1.0f : .25f};
        if (slot==1) {
            /* 00827B10 actor+28 approaches 128/0 by exactly 8 each
             * active tick. Positive levels use green; zero uses red. */
            if (em_game_terminal_powered()) {
                if (indicator->level<128) indicator->level+=8;
                if (indicator->level>128) indicator->level=128;
            } else {
                if (indicator->level>0) indicator->level-=8;
                if (indicator->level<0) indicator->level=0;
            }
            if (indicator->level) {
                color[0]=0;
                color[1]=(float)indicator->level/128.0f;
            }
        }
        em_effect_color(em_random_next(),color,indicator->tint);
        indicator->visible=1;
    }
}

void em_props_indicators_draw(EmGfx *gfx, const float viewproj[16])
{
    for (int slot=0;slot<2;++slot) {
        PropIndicator *indicator=&indicators[slot];
        const float *parent=slot==0 ? g.grate_palette : g.elev_palette;
        if (!indicator->visible || !parent) continue;
        /* 001C6380 stamps the fixed panel placement; 00827B10 copies
         * its parent's node0 world matrix to its indicator while moving. */
        for (uint32_t b=0;b<indicator->model.bone_count;++b)
            memcpy(indicator->palette+b*16,parent,16*sizeof(float));
        em_gfx_draw_skinned_additive(gfx,indicator->mesh,viewproj,
            indicator->palette,indicator->model.bone_count,indicator->tint);
    }
}
