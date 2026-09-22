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
    puts("lighting: PASS authored normals, integer colors, basis scale, clamp and invalid pose");
    return 0;
}
