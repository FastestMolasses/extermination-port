#include "game/em_interaction_scan.h"
#include "game/em_effect_color.h"

#include <math.h>
#include <float.h>

#pragma STDC FP_CONTRACT OFF

int em_interaction_scan(EmInteractionScanState *state,
                        const EmInteractionCandidate *candidates, size_t count,
                        EmInteractionPredicate predicate, void *context,
                        size_t *winner_index)
{
    if (winner_index) *winner_index=SIZE_MAX;
    if (!state) return -1;
    if (state->selector || state->fade_wait || state->inhibited) return 0;
    if (count>EM_INTERACTION_CAPACITY ||
        (count && (!candidates || !predicate))) return -1;
    for (size_t i=0;i<count;++i) {
        if (!candidates[i].armed) return -1;
    }

    float best=10000.0f;
    size_t winner=SIZE_MAX;
    state->score=0.0f;
    for (size_t i=0;i<count;++i) {
        const EmInteractionCandidate *candidate=&candidates[i];
        if (!(candidate->status&1) || !(candidate->class_flags&0x80) ||
            *candidate->armed) continue;
        int result=predicate(context,candidate,&state->score);
        if (!result) continue;
        if (result==2) {
            winner=i;
            break;
        }
        if (state->score<best) {
            best=state->score;
            winner=i;
        }
    }
    if (winner==SIZE_MAX) return 0;
    *candidates[winner].armed=4;
    state->selector=3;
    if (winner_index) *winner_index=winner;
    return 1;
}

static float wrap_angle(float angle)
{
    const float pi=3.1415927410125732421875f;
    const float turn=6.283185482025146484375f;
    while (angle>pi) angle=em_effect_float32((double)angle-turn);
    while (angle<=-pi) angle=em_effect_float32((double)angle+turn);
    return angle;
}

int em_interaction_elevator_candidate(const float descriptor[6],
                                      const float player[3], float player_yaw,
                                      uint8_t player_action, float *score)
{
    /* Action2D restricts the entire original predicate to class7. This
     * elevator is class4, so it never enters that special raycast path. */
    if (player_action==0x2d) return 0;
    if (!descriptor || !player || !isfinite(player_yaw) ||
        !isfinite(descriptor[5])) return 0;
    float dx=em_effect_float32((double)player[0]-descriptor[0]);
    float dz=em_effect_float32((double)player[2]-descriptor[2]);
    float xx=em_effect_float32((double)dx*dx);
    float zz=em_effect_float32((double)dz*dz);
    /* SDK0011E748 ->0011CB90 rounds sqrt to nearest/even. The EE
     * subtracts, multiplies and adds preceding it truncate separately. */
    float distance=sqrtf(em_effect_float32((double)xx+zz));
    if (!(distance<=descriptor[3])) return 0;
    if (score) *score=distance;
    float dy=em_effect_float32((double)player[1]-descriptor[1]);
    if (!(sqrtf(em_effect_float32((double)dy*dy))<=descriptor[4])) return 0;
    float angle=em_effect_float32(3.1415927410125732421875+(double)player_yaw);
    angle=wrap_angle(em_effect_float32((double)angle-descriptor[5]));
    return fabsf(angle)<=0.785398185253143310546875f;
}

static float add(float a,float b) { return em_effect_float32((double)a+b); }
static float sub(float a,float b) { return em_effect_float32((double)a-b); }
static float mul(float a,float b) { return em_effect_float32((double)a*b); }
static float divide(float a,float b) { return (float)((double)a/b); }
static uint32_t float_bits(float x) { uint32_t u;memcpy(&u,&x,4);return u; }

static float dot(const float a[3],const float b[3])
{
    return add(add(mul(a[0],b[0]),mul(a[1],b[1])),mul(a[2],b[2]));
}

static void normalize(float vector[3])
{
    /* 00102760 uses VU sqrt and division, both truncating, unlike the
     * scalar SDK sqrt and EE DIV.S used elsewhere in this module. */
    float length=em_effect_float32(sqrt((double)dot(vector,vector)));
    float inverse=length ? em_effect_float32(1.0/(double)length) : FLT_MAX;
    for (unsigned i=0;i<3;++i) vector[i]=mul(vector[i],inverse);
}

static float sdk_atan(const EmInteractionMath *m,float x)
{
    uint32_t word=float_bits(x),magnitude=word&0x7fffffffu;
    int index=-1;
    if (magnitude>0x507fffffu) {
        float angle=add(m->atan_high[3],m->atan_low[3]);
        return word>>31 ? -angle : angle;
    }
    if (magnitude<=0x3edfffffu) {
        if (magnitude<=0x30ffffffu && 1.0f<add(x,1.0e30f)) return x;
    } else {
        x=fabsf(x);
        if (magnitude<=0x3f97ffffu) {
            if (magnitude<=0x3f2fffffu) {
                index=0;x=divide(sub(mul(2,x),1),add(2,x));
            } else { index=1;x=divide(sub(x,1),add(x,1)); }
        } else if (magnitude<=0x401bffffu) {
            index=2;x=divide(sub(x,1.5f),add(1,mul(1.5f,x)));
        } else { index=3;x=divide(-1,x); }
    }
    float z=mul(x,x),w=mul(z,z);
    float even=m->atan_coefficients[10],odd=m->atan_coefficients[9];
    for (int i=8;i>=0;i-=2) even=add(m->atan_coefficients[i],mul(w,even));
    for (int i=7;i>=1;i-=2) odd=add(m->atan_coefficients[i],mul(w,odd));
    float correction=mul(x,add(mul(z,even),mul(w,odd)));
    if (index<0) return sub(x,correction);
    float angle=sub(m->atan_high[index],sub(sub(correction,m->atan_low[index]),x));
    return word>>31 ? -angle : angle;
}

float em_interaction_sdk_atan2(const EmInteractionMath *m,float y,float x)
{
    const float pi=3.141592502593994140625f;
    const float low=1.509957883172319270670413970947265625e-7f;
    const float half=1.57079637050628662109375f;
    uint32_t hx=float_bits(x),hy=float_bits(y);
    uint32_t ix=hx&0x7fffffffu,iy=hy&0x7fffffffu;
    unsigned quadrant=((hy>>31)&1)|((hx>>30)&2);
    if (hx==0x3f800000u) return sdk_atan(m,y);
    if (!iy) return quadrant<2 ? y : quadrant==2 ? pi : -pi;
    if (!ix) return hy>>31 ? -half : half;
    int exponent=((int32_t)iy-(int32_t)ix)>>23;
    float angle;
    if (exponent>60) angle=add(half,mul(.5f,low));
    else if (hx>>31 && exponent<-60) angle=0;
    else angle=sdk_atan(m,fabsf(divide(y,x)));
    if (quadrant==0) return angle;
    if (quadrant==1) return -angle;
    if (quadrant==2) return sub(pi,sub(angle,low));
    return sub(sub(angle,low),pi);
}

static int query(EmInteractionRaycast raycast,void *context,
                 const EmInteractionPlayer *player,const EmInteractionPickup *pickup,
                 float height,EmInteractionRayHit *hit)
{
    if (!raycast) return 0;
    float from[4]={player->position[0],add(player->position[1],height),player->position[2],1};
    float to[4]={pickup->position[0],pickup->position[1],pickup->position[2],1};
    memset(hit,0,sizeof *hit);
    return raycast(context,from,to,6,hit);
}

int em_interaction_pickup_candidate(const EmInteractionPickup *pickup,
                                    const EmInteractionPlayer *player,
                                    const EmInteractionMath *math,
                                    EmInteractionRaycast raycast,void *context,
                                    float *score)
{
    if (!pickup || !player || !math || !isfinite(player->yaw)) return -1;
    unsigned kind=pickup->class_flags&0x1f;
    if (player->action==0x2d) {
        if (kind!=7) return 0;
        EmInteractionRayHit hit;
        if (!query(raycast,context,player,pickup,1,&hit)) return -1;
        if (hit.hit && (hit.flags&0x2000)) return 0;
        float view[3],toward[3],raw[3];
        for (unsigned i=0;i<3;++i) {
            view[i]=sub(player->view_target[i],player->position[i]);
            toward[i]=raw[i]=sub(pickup->position[i],player->position[i]);
        }
        normalize(view);normalize(toward);
        float alignment=dot(view,toward),squared=dot(raw,raw);
        if (!(squared<=144)) return 0;
        if (squared<4) return 2;
        if (alignment<0 || (alignment<.4f && !(squared<=9))) return 0;
        return 2;
    }
    if (pickup->selector!=3 && pickup->selector!=4) return -1;
    float dx=sub(pickup->position[0],player->position[0]);
    float dz=sub(pickup->position[2],player->position[2]);
    float distance=sqrtf(add(mul(dx,dx),mul(dz,dz)));
    if (!(distance<=pickup->descriptor[0])) return 0;
    if (score) *score=distance;
    float dy=sub(player->position[1],pickup->position[1]);
    if (dy>=0) { if (!(dy<=pickup->descriptor[1])) return 0; }
    else if (!(fabsf(dy)<=add(17,pickup->descriptor[1]))) return 0;
    float angle;
    if (pickup->selector==4) angle=wrap_angle(sub(player->yaw,pickup->angles[1]));
    else if (pickup->subtype==1 || (pickup->subtype==2 &&
             (pickup->class_flags&15)==7 &&
             fmaxf(fabsf(pickup->angles[0]),fabsf(pickup->angles[2]))<=0.7853981852531433f)) {
        angle=wrap_angle(sub(add(3.1415927410125732421875f,player->yaw),pickup->angles[1]));
    } else if (distance<=7) angle=0;
    else angle=wrap_angle(sub(player->yaw,em_interaction_sdk_atan2(math,dx,dz)));
    if (!(fabsf(angle)<=1.57079637050628662109375f)) return 0;
    if ((pickup->class_flags&15)==7 || pickup->callback==0x219550) {
        EmInteractionRayHit hit;
        if (!query(raycast,context,player,pickup,16,&hit)) return -1;
        if (hit.hit && (hit.flags&0x2800) &&
            (hit.kind!=2 || hit.owner!=pickup->identity)) return 0;
    }
    return 1;
}

int em_interaction_visible(const float position[3],const float camera_anchor[3],
                            const float camera_forward[3])
{
    float direction[3];
    for (unsigned i=0;i<3;++i) direction[i]=sub(position[i],camera_anchor[i]);
    float distance=sqrtf(dot(direction,direction));
    if (!(distance<=350)) return 0;
    normalize(direction);
    float facing=dot(direction,camera_forward);
    if (distance<35) return 1;
    return distance<45 ? !(facing<0) : !(facing<.7f);
}

void em_interaction_list_push(EmInteractionList *list,
                              const EmInteractionCandidate *candidate)
{
    if (list->pending_count<EM_INTERACTION_CAPACITY)
        list->pending[list->pending_count++]=*candidate;
}

void em_interaction_list_publish(EmInteractionList *list)
{
    list->active_count=list->pending_count;
    for (size_t i=0;i<list->pending_count;++i)
        list->active[i]=list->pending[list->pending_count-i-1];
    list->pending_count=0;
}
