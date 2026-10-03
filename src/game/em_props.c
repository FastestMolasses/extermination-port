/* em_props.c - see em_props.h. */

#include "game/em_props.h"

#include "game/em_game_internal.h"

static EmCollCell panel_cell;

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
    (void)gfx;
    g.grate_present=0;
}
