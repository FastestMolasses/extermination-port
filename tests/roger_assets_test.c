#include "game/em_roger_assets.h"
#include "game/em_player_pose.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    EmRogerAssets assets={0};
    assert(!em_roger_assets_load(&assets,"build/missing-original-roger"));
    assert(!assets.model.verts && !assets.animation.clips && !assets.programs.bytes);
    assert(em_roger_assets_load(&assets,"assets/scene_snow/roger"));
    assert(assets.model.vert_count==2808 && assets.model.index_count==3380*3);
    assert(assets.trigger[0][0]==358 && assets.trigger[2][0]==330);
    EmPlayerPose pose;
    assert(em_player_pose_init(&pose,&assets.animation,8,0));
    float palette[22*16];
    for (unsigned tick=0;tick<720;++tick) {
        assert(em_player_pose_advance(&pose,tick&1?0.5f:1.0f,0));
        assert(em_player_pose_palette(&pose,palette,22));
    }
    assert(em_player_pose_select(&pose,8,0,20,1));
    /* C64F0 tests remaining<=1 before subtracting the half-rate step. */
    for (unsigned tick=0;tick<39;++tick) assert(em_player_pose_advance(&pose,0.5f,0));
    assert(!pose.transition.active && pose.playback.remaining==180);
    assert(em_player_pose_advance(&pose,0.5f,0));
    assert(pose.playback.remaining==179.5f);
    em_roger_assets_free(&assets);
    assert(!assets.model.verts && !assets.animation.clips && !assets.programs.bytes);
    puts("Original Roger asset load, channel loops/transition and cleanup PASS");
    return 0;
}
