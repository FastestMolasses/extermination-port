/* em_props.c - see em_props.h. Source mapping and validation:
 * docs/OPENING_SCENERY.md, docs/OWNER_DRAW.md sections 10 and 11. */

#include "game/em_props.h"

#include "game/em_game_internal.h"
#include "game/em_effect_color.h"

typedef struct {
    EmModel model;
    EmGfxMesh *mesh;
    float *palette;
    int visible;      /* submitted this frame (em_props_indicator_submit) */
    float tint[4];
    float node[16];   /* the child's slot 0 +0x90 at the submit */
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

int grate_install(const char *scene_dir)
{
    em_collision_cell_unbind(&g.coll,panel_cell.uid);
    em_collision_cell_free(&panel_cell);
    g.grate_present=0;
    char path[1024];
    snprintf(path,sizeof path,"%s/props/panel_cell18.emcb",scene_dir);
    if (em_collision_cell_load(&panel_cell,path)!=0) {
        fprintf(stderr,"panel: missing original cell18 collision: %s\n",path);
        return -1;
    }
    g.grate_present=1;
    return 0;
}

void grate_update(void)
{
    /* Original 00159210's live owner is published in D275B7C;
     * its uid18 was verified in the original first-control state. Its battery/menu phase does not remove the cell. */
    if (g.grate_present) em_collision_cell_bind(&g.coll,&panel_cell);
}

void grate_unload(EmGfx *gfx)
{
    em_collision_cell_unbind(&g.coll,panel_cell.uid);
    em_collision_cell_free(&panel_cell);
    for (int slot=0;slot<2;++slot) indicator_unload(gfx,&indicators[slot]);
    g.grate_present=0;
}

int em_props_indicator_install(EmGfx *gfx, const char *scene_dir,
                                const char *kind, const char *file)
{
    if (!kind || !gfx || !scene_dir || !file) return -1;
    int slot=strcmp(kind,"panel")==0 ? 0 : strcmp(kind,"elevator")==0 ? 1 : -1;
    if (slot<0) return -1;
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

int em_props_indicator_submit(int slot, const float c80[4], const float node[16])
{
    /* The +0x4C draw (001CACB0) of the panel's 0x75 child (slot 0) or the
     * terminal's 0x10 child (slot 1), reached from 001F54E0 inside the
     * child's own node (em_area11_bindings.c tick_indicator). */
    if (slot<0 || slot>1 || !c80 || !node || !indicators[slot].mesh) return -1;
    em_effect_color_gs(c80,indicators[slot].tint);
    memcpy(indicators[slot].node,node,sizeof indicators[slot].node);
    indicators[slot].visible=1;
    return 0;
}

void em_props_indicators_draw(EmGfx *gfx, const float viewproj[16])
{
    for (int slot=0;slot<2;++slot) {
        PropIndicator *indicator=&indicators[slot];
        if (!indicator->visible) continue;
        /* The child's node 0 world matrix (the original row layout, row 3
         * the translation: the palette's column-major matrix): the
         * vertices of models 0x75 and 0x10 all name node 0. */
        for (uint32_t b=0;b<indicator->model.bone_count;++b)
            memcpy(indicator->palette+b*16,indicator->node,16*sizeof(float));
        em_gfx_draw_skinned_additive(gfx,indicator->mesh,viewproj,
            indicator->palette,indicator->model.bone_count,indicator->tint);
        indicator->visible=0; /* one draw per submitted 001F54E0 */
    }
}
