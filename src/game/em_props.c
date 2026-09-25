/* Original AREA11 elevator/switch models and their indicator children.
 * Source mapping and validation: docs/OPENING_SCENERY.md. The historical
 * grate_* names survive only at the scene boundary; no guessed gate
 * motion or synthetic collision box is attached to that switch model. */

#include "game/em_props.h"

#include "game/em_game_internal.h"
#include "game/em_effect_color.h"

typedef struct {
    EmModel model;
    EmGfxMesh *mesh;
    float *palette;
    int visible;      /* submitted this frame (em_props_indicator_submit) */
    float tint[4];
} PropIndicator;

static PropIndicator indicators[2]; /* model75 panel; model10 elevator */
static EmCollCell panel_cell;

static void indicator_unload(EmGfx *gfx, PropIndicator *indicator)
{
    if (indicator->mesh) em_gfx_mesh_destroy(gfx,indicator->mesh);
    em_model_free(&indicator->model);
    free(indicator->palette);
    memset(indicator,0,sizeof *indicator);
}

/* 00827B10 owns per-area model0F. Its carry 00828050 (the host's
 * elevator program, em_elevator_runtime) moves this same actor through
 * g.elev_pos[1]; its model10 child copies the parent's node0 world matrix.
 * The legacy 150-frame ride (elevator_tick/elevator_descent_begin) that
 * stood in for the owner was retired in WP-4. */
void elevator_pose(void)
{
    if (!g.elev_has_mesh || !g.elev_palette) return;
    em_model_palette_at(&g.elev_model,0,0.0,g.elev_palette);
    palette_apply_placement(g.elev_palette,g.elev_model.bone_count,
                            g.elev_pos,g.elev_yaw);
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
    snprintf(path,sizeof path,"%s/props/panel_cell18.emcb",scene_dir);
    if (em_collision_cell_load(&panel_cell,path)!=0) {
        fprintf(stderr,"panel: missing original cell18 collision: %s\n",path);
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
    /* Original 00159210's live owner is published in D275B7C;
     * its uid18 was verified in the original first-control state. Its battery/menu phase does not remove the cell. */
    if (g.grate_present) em_collision_cell_bind(&g.coll,&panel_cell);
}

void grate_unload(EmGfx *gfx)
{
    em_collision_cell_unbind(&g.coll,panel_cell.uid);
    em_collision_cell_free(&panel_cell);
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
    return 0;
}

int em_props_indicator_submit(int slot, const float c80[4])
{
    /* The +0x4C draw (001CACB0) of the panel's 0x75 child (slot 0) or the
     * terminal's 0x10 child (slot 1), reached from 001F54E0 inside the
     * child's own node (em_area11_bindings.c tick_indicator). */
    if (slot<0 || slot>1 || !c80 || !indicators[slot].mesh) return -1;
    em_effect_color_gs(c80,indicators[slot].tint);
    indicators[slot].visible=1;
    return 0;
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
        indicator->visible=0; /* one draw per submitted 001F54E0 */
    }
}
