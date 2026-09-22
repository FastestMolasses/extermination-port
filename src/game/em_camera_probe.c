#include "game/em_camera_probe.h"
#include "game/em_effect_color.h"

static void normalize(float vector[3])
{
    /* Explicit helper boundary: bounded finite vector normalization. The
     * original is a VU helper; this does not claim its physical rounding. */
    float square=em_effect_float32((double)vector[0]*vector[0]);
    square=em_effect_float32((double)square+(double)vector[1]*vector[1]);
    square=em_effect_float32((double)square+(double)vector[2]*vector[2]);
    if (square==0) return;
    float inverse=em_effect_float32(1.0/(double)sqrtf(square));
    for (int i=0;i<3;++i)
        vector[i]=em_effect_float32((double)vector[i]*inverse);
}

int em_camera_interaction_probe(EmCameraProbe *probe,const float eye[3],
    const float position[3],const float hip[3],EmCameraProbeQuery query,void *context)
{
    if (!probe || !query) return 0;
    float from[3]={hip[0],em_effect_float32((double)hip[1]+4),hip[2]};
    float to[3]={hip[0],em_effect_float32((double)position[1]-2),hip[2]};
    EmCollHit hit;
    int result=query(context,from,to,1,&hit);
    if (result<0) return 0;
    probe->ground78=result!=0;
    to[1]=em_effect_float32((double)hip[1]+200);
    result=query(context,hip,to,0,&hit);
    if (result<0) return 0;
    uint16_t flags=0;
    if (result && (hit.surf_class&0x8800)) {
        flags=0x80;
        probe->overhead_y=hit.point[1];
    }
    from[0]=position[0];from[1]=em_effect_float32((double)position[1]+11);from[2]=position[2];
    float direction[3]={em_effect_float32((double)eye[0]-from[0]),0,
                         em_effect_float32((double)eye[2]-from[2])};
    normalize(direction);
    for (int i=0;i<3;++i) {
        direction[i]=em_effect_float32((double)direction[i]*20);
        to[i]=em_effect_float32((double)direction[i]+from[i]);
    }
    result=query(context,from,to,0,&hit);
    if (result<0) return 0;
    if (result) {
        if (hit.surf_class&0x2000) flags|=1;
        else if (hit.surf_class&0x8800) flags|=8;
    }
    probe->flags=flags;
    return 1;
}

int em_camera_interaction_bounds11(float bounds[2],const float eye[3],
    const float position[3],float height_variant,EmCameraProbeQuery query,void *context)
{
    if (!bounds || !query) return 0;
    float direction[3]={em_effect_float32((double)eye[0]-position[0]),
        em_effect_float32((double)eye[1]-em_effect_float32((double)position[1]+11)),
        em_effect_float32((double)eye[2]-position[2])};
    normalize(direction);
    float from[3],to[3];
    for (int i=0;i<3;++i) from[i]=to[i]=em_effect_float32((double)eye[i]-direction[i]);
    to[1]=em_effect_float32((double)from[1]-200);
    EmCollHit hit;
    int result=query(context,from,to,0,&hit);
    if (result<0) return 0;
    float low=result && (hit.surf_class&0x7000) ?
        em_effect_float32((double)hit.point[1]+(height_variant==1 ? 6 : 17)) :
        em_effect_float32((double)bounds[0]-200);
    to[1]=em_effect_float32((double)from[1]+200);
    result=query(context,from,to,0,&hit);
    if (result<0) return 0;
    float high=result && (hit.surf_class&0x8800) ?
        em_effect_float32((double)hit.point[1]-1) : em_effect_float32((double)eye[1]+200);
    if (low>high) low=em_effect_float32((double)high-3);
    bounds[0]=low;bounds[1]=high;
    return 1;
}
