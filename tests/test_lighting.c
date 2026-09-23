#include "game/em_lighting.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const float directions[12] = {1,0,0,0, 0,1,0,0, 0,0,1,0};
    const float colors[12] = {64,0,0,0, 0,128,0,0, 0,0,256,0};
    const float ambient[4] = {32,32,32,0};
    float bone[16] = {2,0,0,0, 0,3,0,0, 0,0,4,0, 5,6,7,1};
    EmLightingMatrices matrices;
    assert(em_lighting_matrices(&matrices,bone,directions,colors,ambient));
    uint32_t rgba[4];
    const float authored[3] = {.25f,.5f,1.f};
    em_lighting_vertex(rgba,authored,&matrices);
    /* Truncated1/3 makes that normalized basis one ULP below1, so its
     * quantized green is95 rather than the ideal real-arithmetic96. */
    assert((rgba[0]&255)==48 && (rgba[1]&255)==95 && (rgba[2]&255)==255);
    /* The normal above is deliberately not unit length. The original
     * performs no per-normal normalization before its directional clamps. */
    const float back[3] = {-1,-1,-1};
    em_lighting_vertex(rgba,back,&matrices);
    for (unsigned c=0;c<3;++c) assert((rgba[c]&255)==32);
    assert((rgba[3]&255)==0);
    memset(bone,0,sizeof bone);
    assert(em_lighting_matrices(&matrices,bone,directions,colors,ambient));
    em_lighting_vertex(rgba,authored,&matrices);
    for (unsigned c=0;c<3;++c) assert((rgba[c]&255)==32);
    bone[0]=NAN;
    assert(!em_lighting_matrices(&matrices,bone,directions,colors,ambient));
    /* 001D8690: rows and ambient scale by actor RGB before the bias;
     * AREA11 item0b carries actor RGB (4,4,4): ambient 32 -> 128. */
    float rgb_colors[12] = {1.776f,.444f,.111f,0, 74,74,74,0, 38,38,38,0};
    float rgb_ambient[4] = {32,32,32,0};
    const float four[3] = {4,4,4};
    assert(em_lighting_actor_rgb(rgb_colors,rgb_ambient,four));
    assert(rgb_colors[4]==296 && rgb_colors[8]==152 && rgb_ambient[0]==128);
    assert(rgb_colors[0]==1.776f*4 && rgb_colors[3]==0 && rgb_ambient[3]==0);
    float identity_colors[12] = {.1f,.2f,.3f,0, 1,2,3,0, 4,5,6,0};
    float identity_ambient[4] = {7,8,9,0};
    const float one[3] = {1,1,1};
    float before[12]; memcpy(before,identity_colors,sizeof before);
    assert(em_lighting_actor_rgb(identity_colors,identity_ambient,one));
    assert(!memcmp(before,identity_colors,sizeof before) && identity_ambient[2]==9);
    /* Truncating EE multiply: 0.1f*3 is one ULP below round-to-nearest. */
    float trunc_colors[12] = {.1f,0,0,0};
    float trunc_ambient[4] = {0};
    const float three[3] = {3,1,1};
    assert(em_lighting_actor_rgb(trunc_colors,trunc_ambient,three));
    assert(trunc_colors[0] <= .1f*3.f);
    const float bad[3] = {NAN,1,1};
    assert(!em_lighting_actor_rgb(trunc_colors,trunc_ambient,bad));
    /* 001D8270 exclusions and the radius compare. */
    static const unsigned excluded[] = {3,8,9,0xb,0xd,0x15,0x16,0x17,0x3d,0x3e};
    for (unsigned i=0;i<sizeof excluded/sizeof *excluded;++i)
        assert(!em_lighting_fold_gate(excluded[i],1.f));
    assert(em_lighting_fold_gate(0x1a,21.86f) && !em_lighting_fold_gate(0x29,50.25f));
    assert(!em_lighting_fold_gate(1,30.f) && em_lighting_fold_gate(1,29.999998f));
    assert(!em_lighting_fold_gate(0,NAN) && em_lighting_fold_gate(0x103,1.f)==0);
    puts("lighting: PASS authored normals, integer colors, basis scale, clamp, invalid pose, actor RGB and fold gate");
    return 0;
}
