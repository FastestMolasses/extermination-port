#include "game/em_opening_face.h"
#include <string.h>
#pragma STDC FP_CONTRACT OFF

void em_opening_face_init(EmOpeningFace *f, uint8_t speed)
{
    /* Original AF890 clears all208 bytes before returning an object to
     * AF780's pool. Captured first-control/status player allocations are
     * zero, including weights. Existing attached faces instead use the
     * partial reset below; global random-call ordering is separate. */
    memset(f,0,sizeof *f);
    em_opening_face_reset(f);
    f->speed=speed;
}

void em_opening_face_reset(EmOpeningFace *f)
{
    f->talking=0;
    f->blink_state=f->expression_state=0;
    f->mouth_wait=f->current_shape=f->previous_shape=0;
    memset(f->target,0,sizeof f->target);
}

void em_opening_face_talk(EmOpeningFace *f, uint8_t talking)
{
    f->talking=talking;
    if (!talking) memset(f->target,0,sizeof f->target);
}

static float approach(float value, float target, float rate)
{
    float difference=target-value;
    float step=rate*difference;
    return value+step;
}

void em_opening_face_tick(EmOpeningFace *f, EmFaceRandom random, void *context)
{
    /* Constants at original 002513B0 and 002513C0, checked in boot ELF. */
    static const int blink_wait[4]={10,40,90,180};
    static const int expression_wait[4]={40,60,90,120};
    switch (f->blink_state) {
    case 0:
        f->blink_wait=blink_wait[(random(context)>>16)&3];
        f->blink_state=1;
        break;
    case 1:
        if (--f->blink_wait<=0) f->blink_state=2;
        break;
    case 2:
        f->weight[0]=approach(f->weight[0],1.0f,0.4f);
        if (!(f->weight[0]<=0.95f)) {
            f->weight[0]=1.0f;
            f->blink_state=3;
        }
        break;
    case 3:
        f->weight[0]=approach(f->weight[0],0.0f,0.4f);
        if (f->weight[0]<0.05f) {
            f->weight[0]=0.0f;
            f->blink_state=0;
        }
        break;
    }
    switch (f->expression_state) {
    case 0:
        if (!f->talking)
            f->expression_wait=(int)(((random(context)>>16)*90)>>15)+60;
        else
            f->expression_wait=expression_wait[(random(context)>>16)&3];
        f->expression_state=1;
        break;
    case 1:
        if (--f->expression_wait<=0)
            f->expression_state=f->weight[1]<0.5f ? 2 : 3;
        break;
    case 2:
        f->weight[1]=approach(f->weight[1],1.0f,0.1f);
        if (!(f->weight[1]<=0.95f)) {
            f->weight[1]=1.0f;
            f->expression_state=0;
        }
        break;
    case 3:
        f->weight[1]=approach(f->weight[1],0.0f,0.1f);
        if (f->weight[1]<0.05f) {
            f->weight[1]=0.0f;
            f->expression_state=0;
        }
        break;
    default:
        /* Original default and state4 both release when talking starts. */
        if (f->talking) f->expression_state=0;
        break;
    }
    if (f->talking && --f->mouth_wait<0) {
        int shape=(int)((random(context)>>24)%5);
        if (shape==f->previous_shape) shape=(f->previous_shape+1)%5;
        if (shape==f->current_shape) {
            for (int i=1;i<6;++i)
                f->target[i]=i==f->previous_shape ? 0.0f : f->target[i]*0.1f;
        } else {
            /* The original selects 0..4 but updates targets1..5. Preserve
             * this asymmetry: selecting0 deliberately boosts no target. */
            for (int i=1;i<6;++i) {
                float r=(float)((random(context)>>24)&127);
                if (i==shape) f->target[i]=0.8f+r/512.0f;
                else f->target[i]=f->target[i]*(0.1f+r/256.0f);
            }
        }
        f->previous_shape=f->current_shape;
        f->current_shape=shape;
        uint32_t r=random(context)>>16;
        if (f->speed==2) f->mouth_wait=(int)(r&15)+3;
        else if (f->speed==1) f->mouth_wait=(int)(r&7)+3;
        else f->mouth_wait=(int)(r%5)+3;
    }
    float rate=f->speed==2 ? 0.2f : f->speed==1 ? 0.3f : 0.4f;
    for (int i=1;i<6;++i)
        f->weight[i+1]=approach(f->weight[i+1],f->target[i],rate);
}

void em_opening_face_position(float out[3], const float base[3],
                               const float delta[21], const float weight[8])
{
    for (int c=0;c<3;++c) {
        float sum=delta[c]*weight[0];
        for (int i=1;i<7;++i) sum+=delta[3*i+c]*weight[i];
        out[c]=sum+base[c];
    }
}
