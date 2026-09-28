#include "game/em_opening_face.h"
#include "game/em_ee_float.h"
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

/* 001D0720's float arithmetic is COP1 (sub.s, mul.s, add.s, div.s,
 * cvt.s.w): em_ee_float.h, the measured EE model (docs/EE_FLOAT_MODEL.md). */
static float approach(float value, float target, float rate)
{
    float difference=em_ee_sub(target,value);
    float step=em_ee_mul(rate,difference);
    return em_ee_add(value,step);
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
                f->target[i]=i==f->previous_shape ? 0.0f : em_ee_mul(f->target[i],0.1f);
        } else {
            /* The original selects 0..4 but updates targets1..5. Preserve
             * this asymmetry: selecting0 deliberately boosts no target. */
            for (int i=1;i<6;++i) {
                float r=em_ee_cvt_s_w((int32_t)((random(context)>>24)&127));
                if (i==shape) f->target[i]=em_ee_add(0.8f,em_ee_div(r,512.0f));
                else f->target[i]=em_ee_mul(f->target[i],em_ee_add(0.1f,em_ee_div(r,256.0f)));
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

/* The slot's bytes <-> EmOpeningFace (the offsets in em_opening_face.h),
 * around one 001D0720 step. */
void em_opening_face_tick_slot(uint8_t *slot, EmFaceRandom random, void *context)
{
    EmOpeningFace f;
    memcpy(f.weight, slot + 0x40, sizeof f.weight);
    memcpy(&f.blink_state, slot + 0x70, 4);
    memcpy(&f.blink_wait, slot + 0x74, 4);
    memcpy(&f.expression_state, slot + 0x78, 4);
    memcpy(&f.expression_wait, slot + 0x7C, 4);
    f.talking = slot[0x80];
    f.speed = slot[0x81];
    f.reserved[0] = slot[0x82];
    f.reserved[1] = slot[0x83];
    memcpy(&f.mouth_wait, slot + 0x84, 4);
    memcpy(&f.current_shape, slot + 0x88, 4);
    memcpy(&f.previous_shape, slot + 0x8C, 4);
    memcpy(f.target, slot + 0x90, sizeof f.target);
    em_opening_face_tick(&f, random, context);
    memcpy(slot + 0x40, f.weight, sizeof f.weight);
    memcpy(slot + 0x70, &f.blink_state, 4);
    memcpy(slot + 0x74, &f.blink_wait, 4);
    memcpy(slot + 0x78, &f.expression_state, 4);
    memcpy(slot + 0x7C, &f.expression_wait, 4);
    slot[0x80] = f.talking;
    slot[0x81] = f.speed;
    slot[0x82] = f.reserved[0];
    slot[0x83] = f.reserved[1];
    memcpy(slot + 0x84, &f.mouth_wait, 4);
    memcpy(slot + 0x88, &f.current_shape, 4);
    memcpy(slot + 0x8C, &f.previous_shape, 4);
    memcpy(slot + 0x90, f.target, sizeof f.target);
}
