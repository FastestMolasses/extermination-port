/* Headless check of locally exported opening assets and lifetime. The mesh
 * backend is stubbed; the actual EMDL loader and playback module are linked.
 * Run after tools/export_opening_actors.py from the companion decomp repo. */
#include "game/em_opening_actor.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned live_meshes, draws, updates;

static void missing_face_test(EmGfx *gfx,const char *scene)
{
    char absolute[PATH_MAX],directory[]="/tmp/em-opening-face-XXXXXX";
    char source[PATH_MAX],target[PATH_MAX],opening[PATH_MAX];
    static const char *files[]={"player.emdl","roger.emdl","equipment_6b.emdl",
        "player_face.emdl","player_face.emfm","roger_face.emdl"};
    assert(realpath(scene,absolute) && mkdtemp(directory));
    assert(snprintf(opening,sizeof opening,"%s/opening",directory)>0);
    assert(!mkdir(opening,0700));
    for(unsigned i=0;i<sizeof files/sizeof files[0];++i) {
        assert(snprintf(source,sizeof source,"%s/opening/%s",absolute,files[i])<(int)sizeof source);
        assert(snprintf(target,sizeof target,"%s/%s",opening,files[i])<(int)sizeof target);
        assert(!symlink(source,target));
    }
    assert(!em_opening_actor_init(gfx,directory));
    assert(!live_meshes);
    for(unsigned i=0;i<sizeof files/sizeof files[0];++i) {
        assert(snprintf(target,sizeof target,"%s/%s",opening,files[i])>0);
        assert(!unlink(target));
    }
    assert(!rmdir(opening) && !rmdir(directory));
}

EmGfxMesh *em_gfx_mesh_create(EmGfx *gfx, const float *vertices,
    uint32_t vertex_count, const uint32_t *indices, uint32_t index_count,
    const EmGfxTexDesc *textures, uint32_t texture_count,
    const uint8_t *texels, uint32_t flags)
{
    (void)gfx;
    assert(vertices && vertex_count && indices && index_count);
    assert(textures && texture_count && texels && flags==0);
    unsigned faces=0;
    for(uint32_t i=0;i<vertex_count;++i) {
        uint32_t bone;
        memcpy(&bone,vertices+(size_t)i*10+8,sizeof bone);
        if(bone&EM_GFX_VERT_FACE_LIGHT) {
            assert((bone&EM_GFX_VERT_BONE_MASK)==7);
            ++faces;
        }
    }
    assert(faces==(live_meshes==0?850u:live_meshes==1?701u:0u));
    ++live_meshes;
    uint32_t *mesh=malloc(sizeof *mesh);assert(mesh);*mesh=vertex_count;
    return (EmGfxMesh *)mesh;
}

int em_gfx_mesh_update_positions(EmGfx *gfx,EmGfxMesh *mesh,
                                 const float *positions,uint32_t count)
{
    assert(gfx && mesh && positions && count==*(uint32_t *)mesh);
    for(size_t i=0;i<(size_t)count*3;++i) assert(isfinite(positions[i]));
    ++updates;
    return 1;
}

void em_gfx_mesh_destroy(EmGfx *gfx, EmGfxMesh *mesh)
{
    (void)gfx;
    assert(mesh && live_meshes);
    --live_meshes;
    free(mesh);
}

void em_gfx_draw_skinned(EmGfx *gfx, EmGfxMesh *mesh, const float *viewproj,
                        const float *palette, uint32_t bone_count)
{
    (void)gfx;
    assert(mesh && viewproj && palette && (bone_count==22 || bone_count==2));
    ++draws;
}

int main(int argc, char **argv)
{
    const char *scene=argc>1 ? argv[1] : "assets/scene_snow";
    EmGfx *gfx=(EmGfx *)&live_meshes;
    EmGfxMesh *mesh;
    const float *player, *roger, *equipment, *held;
    uint32_t bones;
    assert(!em_opening_actor_record(0,0,&mesh,&player,&bones));
    assert(em_opening_actor_init(gfx,scene));
    assert(live_meshes==3);
    assert(!em_opening_actor_tick(1,0)); /* do not silently skip face ticks */
    for (uint32_t tick=0;tick<=1290;++tick) {
        assert(em_opening_actor_tick(tick,(tick/90)%3));
        assert(em_opening_actor_tick(tick,(tick/90)%3)); /* duplicate draw is inert */
        assert(updates==(tick+1)*2);
        assert(em_opening_actor_record(0,tick,&mesh,&player,&bones) && bones==22);
        assert(em_opening_actor_record(1,tick,&mesh,&roger,&bones) && bones==22);
        assert(em_opening_actor_record(2,tick,&mesh,&equipment,&bones) && bones==2);
        /* Original 001C5C90 copies Roger bone1 world matrix, no offsets. */
        assert(memcmp(equipment,roger+16,16*sizeof(float))==0);
        for (unsigned i=0;i<22*16;++i) assert(isfinite(player[i]));
        for (unsigned i=0;i<22*16;++i) assert(isfinite(roger[i]));
    }
    assert(player[28]==251.287109375f);
    assert(player[29]==241.669921875f);
    assert(player[30]==211.1787109375f);
    assert(em_opening_actor_record(0,UINT32_MAX,&mesh,&held,&bones));
    assert(held==player); /* original clip header -2 holds, never wraps */
    assert(!em_opening_actor_record(3,0,&mesh,&held,&bones));
    assert(!em_opening_actor_record(0,0,NULL,&held,&bones));
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    em_opening_actor_draw(gfx,identity,270);
    assert(draws==3);
    em_opening_actor_begin();
    assert(em_opening_actor_tick(0,0));
    assert(updates==1292*2);
    em_opening_actor_shutdown(gfx);
    assert(!live_meshes);
    assert(!em_opening_actor_record(0,0,&mesh,&held,&bones));
    assert(!em_opening_actor_init(gfx,"build/no-such-opening-fixture"));
    assert(!live_meshes);
    missing_face_test(gfx,scene);
    puts("opening actors: 1291 poses, original attachment, final hold, lifetime PASS");
    return 0;
}
