#include "game/em_lighting.h"
#include "game/em_actor_light_001D89D0.h"
#include "game/em_ee_float.h"

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
    /* 001D8690 (its one translation, em_actor_light_001D8690): rows and
     * ambient scale by actor RGB before the bias; AREA11 item0b carries
     * actor RGB (4,4,4): ambient 32 -> 128. The rig record D_00817BC0 holds
     * the slot colours at +0xF0 and the ambient at +0x120. */
    uint32_t rig[EM_ACTOR_LIGHT_RIG_WORDS], a[16], b[16];
    EmActorLight light;
    memset(&light,0,sizeof light);
    light.world.d00817BC0 = rig;
    const float rgb_colors[12] = {1.776f,.444f,.111f,0, 74,74,74,0, 38,38,38,0};
    memset(rig,0,sizeof rig);
    memcpy(rig+0xF0/4,rgb_colors,sizeof rgb_colors);
    const float rgb_ambient[3] = {32,32,32};
    memcpy(rig+0x120/4,rgb_ambient,sizeof rgb_ambient);
    const float four[4] = {4,4,4,1};
    assert(em_actor_light_001D8690(&light,a,b,(const uint32_t *)(const void *)four)==0);
    float bf[16]; memcpy(bf,b,sizeof bf);
    assert(bf[4]==296 && bf[8]==152 && bf[12]==8388608.0f+128);
    assert(bf[0]==1.776f*4 && bf[3]==0 && bf[15]==8388608.0f);
    const float identity_colors[12] = {.1f,.2f,.3f,0, 1,2,3,0, 4,5,6,0};
    const float identity_ambient[3] = {7,8,9};
    memcpy(rig+0xF0/4,identity_colors,sizeof identity_colors);
    memcpy(rig+0x120/4,identity_ambient,sizeof identity_ambient);
    const float one[4] = {1,1,1,1};
    assert(em_actor_light_001D8690(&light,a,b,(const uint32_t *)(const void *)one)==0);
    memcpy(bf,b,sizeof bf);
    for (unsigned r=0;r<3;++r)
        for (unsigned c=0;c<3;++c) assert(bf[4*r+c]==identity_colors[4*r+c]);
    assert(bf[14]==8388608.0f+9);
    /* EE multiply (em_ee_float.h, measured): 0.1f*3 is not above the host's
     * round-to-nearest product. */
    const float trunc_colors[12] = {.1f,0,0,0};
    memset(rig,0,sizeof rig);
    memcpy(rig+0xF0/4,trunc_colors,sizeof trunc_colors);
    const float three[4] = {3,1,1,1};
    assert(em_actor_light_001D8690(&light,a,b,(const uint32_t *)(const void *)three)==0);
    memcpy(bf,b,sizeof bf);
    assert(bf[0] <= .1f*3.f);
    /* The original multiplies whatever the actor holds (no validation in
     * 001D8690): an all-ones-exponent lane is an EE operand like any other
     * (em_ee_float.h's mul.s), not a refused input. */
    const float bad[4] = {NAN,1,1,1};
    assert(em_actor_light_001D8690(&light,a,b,(const uint32_t *)(const void *)bad)==0);
    assert(b[0]==em_ee_mul_bits(rig[0xF0/4],em_ee_bits(NAN)));
    /* 001D8270 (em_actor_light_001D8270): exclusions and the radius
     * compare (c.lt.s against 30.0: NaN does not fold). The type is the
     * byte +3, so 0x103 is type 0x03. */
    static const unsigned excluded[] = {3,8,9,0xb,0xd,0x15,0x16,0x17,0x3d,0x3e};
    struct { unsigned type; float radius; int fold; } gate_cases[] = {
        {0x1a,21.86f,1}, {0x29,50.25f,0}, {1,30.f,0}, {1,29.999998f,1}, {0,NAN,0}, {0x103&0xff,1.f,0}};
    for (unsigned i=0;i<sizeof excluded/sizeof *excluded+sizeof gate_cases/sizeof *gate_cases;++i) {
        unsigned n = sizeof excluded/sizeof *excluded;
        EmActorLightOwner owner;
        memset(&owner,0,sizeof owner);
        float radius = i<n ? 1.f : gate_cases[i-n].radius;
        uint32_t radius_word; memcpy(&radius_word,&radius,4);
        owner.kind = (uint8_t)(i<n ? excluded[i] : gate_cases[i-n].type);
        owner.model_radius = &radius_word;
        int32_t fold = -1;
        assert(em_actor_light_001D8270(&light,&owner,&fold)==0);
        assert(fold==(i<n ? 0 : gate_cases[i-n].fold));
    }
    puts("lighting: PASS authored normals, integer colors, basis scale, clamp, invalid pose, actor RGB and fold gate (one owner)");
    return 0;
}
