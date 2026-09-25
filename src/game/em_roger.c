#include "game/em_roger.h"
#include "game/em_effect_color.h"
#include <math.h>
#include <string.h>

static int emit(const EmRogerHooks *h, EmRogerEvent event, unsigned argument)
{ return h->event(h->context,event,argument)==1; }

static int publication(EmRoger *r, const EmRogerHooks *h)
{
    int visible=h->publish(h->context);
    if (visible<0 || visible>1) return 0;
    r->rendered=(uint8_t)visible;
    return 1;
}

int em_roger_tick(EmRoger *r, EmRogerStory *story, const EmRogerHooks *h)
{
    if (!r || !story || !h || !h->script_start || !h->script_tick || !h->trigger ||
        !h->animation_init || !h->animation_tick || !h->publish || !h->event) return -1;
    if (r->freed) return 0;
    if (!r->lifecycle) return -1; /* Original1B10B0/clip8/mode2 init is required. */
    if (r->lifecycle==2 || r->lifecycle==3) {
        if (!emit(h,EM_ROGER_RELEASE_FACE,0) || !emit(h,EM_ROGER_FREE,0)) return -1;
        r->freed=1;
        return 0;
    }
    if (r->lifecycle!=1) return 1; /* Original dispatcher default does nothing. */

    uint16_t advance=0;
    if (!story->progress) {
        if (story->suppressed==1) { r->rendered=0; return 1; }
        if (story->alternate==1 && !story->auxiliary) {
            if (!r->phase) {
                if (h->script_start(h->context,0x828990)!=1) return -1;
                r->phase=1;
            } else if (r->phase==1) {
                int done=h->script_tick(h->context);
                if (done<0 || done>1) return -1;
                if (done) {
                    r->phase=0;
                    if (h->animation_init(h->context,8,20.0f,0.0f)!=1) return -1;
                }
            }
            if (!emit(h,EM_ROGER_FACE_UPDATE,r->model_kind) ||
                h->animation_tick(h->context,1.0f,&advance)!=1 ||
                !emit(h,EM_ROGER_BUILD_POSE,0) || !publication(r,h) ||
                !emit(h,EM_ROGER_DRAW,r->rendered)) return -1;
            return 1;
        }
        if (story->auxiliary==0x10) {
            const uint32_t bits=0x4036129F;
            memcpy(&r->yaw,&bits,4);
            story->auxiliary=0x11;
        }
        if (!r->phase) {
            int triggered=h->trigger(h->context);
            if (triggered<0 || triggered>1) return -1;
            if (triggered) {
                if (h->script_start(h->context,0x8283D0)!=1 ||
                    !emit(h,EM_ROGER_STOP_STREAMS,0)) return -1;
                r->phase=1;
            }
            if (h->animation_tick(h->context,1.0f,&advance)!=1) return -1;
        } else if (r->phase==1) {
            int done=h->script_tick(h->context);
            if (done<0 || done>1) return -1;
            if (done) {
                if (!emit(h,EM_ROGER_RESTORE_DEFAULT_BANK,0x4A) ||
                    h->animation_init(h->context,8,0.0f,0.0f)!=1) return -1;
                story->progress|=1;
                if (!emit(h,EM_ROGER_RESUME_MUSIC,0)) return -1;
                r->phase=0;
                if (!emit(h,EM_ROGER_FADE_IN,4)) return -1;
            }
            if (h->animation_tick(h->context,0.5f,&advance)!=1) return -1;
            r->animation_result=advance;
        }
        if (!emit(h,EM_ROGER_FACE_UPDATE,r->model_kind) || !publication(r,h)) return -1;
        r->rendered=1;
        if (!emit(h,EM_ROGER_BUILD_POSE,0) || !emit(h,EM_ROGER_DRAW,1)) return -1;
        return 1;
    }
    if (story->progress&0x80) {
        if (!r->phase) {
            if (h->script_start(h->context,0x828A10)!=1) return -1;
            r->phase=1;
        } else if (r->phase==1) {
            int done=h->script_tick(h->context);
            if (done<0 || done>1) return -1;
            if (done) {
                if (!emit(h,EM_ROGER_REMOVE_GROUP,1)) return -1;
                r->lifecycle=3;
            }
        }
    } else {
        if (!r->phase && (r->armed&4)) {
            if (h->script_start(h->context,0x828810)!=1) return -1;
            r->phase=1;
        } else if (r->phase==1) {
            int done=h->script_tick(h->context);
            if (done<0 || done>1) return -1;
            if (done) {
                if (h->animation_init(h->context,8,20.0f,0.0f)!=1) return -1;
                r->armed=0;
                r->phase=0;
            }
        }
        if (!emit(h,EM_ROGER_FACE_UPDATE,r->model_kind) ||
            h->animation_tick(h->context,1.0f,&advance)!=1) return -1;
    }
    if (!publication(r,h) || !emit(h,EM_ROGER_BUILD_POSE,0) ||
        !emit(h,EM_ROGER_DRAW,r->rendered)) return -1;
    return 1;
}

static float sub(float a,float b) { return em_effect_float32((double)a-b); }
static float mul(float a,float b) { return em_effect_float32((double)a*b); }
static float wrap(float a)
{
    while (a>3.1415927410125732421875f) a=sub(a,6.283185482025146484375f);
    while (a<=-3.1415927410125732421875f)
        a=em_effect_float32((double)a+6.283185482025146484375f);
    return a;
}

int em_roger_candidate(const float descriptor[2], const float owner[3],
    const EmInteractionPlayer *player, const EmInteractionMath *math, float *score)
{
    if (!descriptor || !owner || !player || !math || !isfinite(player->yaw)) return -1;
    if (player->action==0x2D) return 0;
    for (unsigned i=0;i<3;++i)
        if (!isfinite(owner[i]) || !isfinite(player->position[i])) return -1;
    float dx=sub(player->position[0],owner[0]),dz=sub(player->position[2],owner[2]);
    float distance=sqrtf(em_effect_float32((double)mul(dx,dx)+mul(dz,dz)));
    if (!(distance<=descriptor[0])) return 0;
    if (score) *score=distance;
    float dy=sub(player->position[1],owner[1]);
    if (!(sqrtf(mul(dy,dy))<=descriptor[1])) return 0;
    return fabsf(wrap(sub(player->yaw,em_interaction_sdk_atan2(math,-dx,-dz))))<=
           0.785398185253143310546875f;
}
