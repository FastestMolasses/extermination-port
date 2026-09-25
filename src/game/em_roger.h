/* Original AREA11 Roger controller after model initialization. */
#ifndef EM_ROGER_H
#define EM_ROGER_H
#include "game/em_interaction_scan.h"

typedef struct {
    uint8_t status, rendered, class_flags, lifecycle, phase, armed, model_kind, freed;
    uint16_t animation_result;
    float yaw;
} EmRoger;

typedef struct {
    uint8_t progress;   /*8107D8 */
    uint8_t suppressed; /*810791: only value1 suppresses initial branch */
    uint8_t alternate;  /*810793 */
    uint8_t auxiliary;  /*810813 */
} EmRogerStory;

typedef enum {
    EM_ROGER_STOP_STREAMS,        /*1FABB0 */
    EM_ROGER_RESTORE_DEFAULT_BANK,/*28A5B8: bank4A in initial AREA11 */
    EM_ROGER_RESUME_MUSIC,        /*1FAE70(0) */
    EM_ROGER_FADE_IN,             /*1AEE10(4,0) */
    EM_ROGER_FACE_UPDATE,         /*1BA580(actor,model_kind): activity slot1 */
    EM_ROGER_BUILD_POSE,          /*1C68C0 */
    EM_ROGER_DRAW,                /*virtual4C, after rendered-byte updates */
    EM_ROGER_REMOVE_GROUP,        /*1B0C60(1,0,4) */
    EM_ROGER_RELEASE_FACE,        /*1BA540: CA770 if extended face, D8BF0(0) */
    EM_ROGER_FREE                /*AFC10 */
} EmRogerEvent;

typedef struct {
    void *context;
    int (*script_start)(void *, uint32_t entry);
    int (*script_tick)(void *); /*-1 fault,0 waiting,1 complete */
    int (*trigger)(void *); /*B1EA0(0,playerXYZ,polygon82AB80,4) */
    int (*animation_init)(void *, uint16_t clip, float blend, float start);
    int (*animation_tick)(void *, float rate, uint16_t *result);
    int (*publish)(void *); /*B17A0 actual visibility0/1, publishing where eligible */
    int (*event)(void *, EmRogerEvent, unsigned argument);
} EmRogerHooks;

/*1 allocated,0 freed,-1 missing worker/uninitialized state/fault.
 * Script workers must execute the real programs; no fallback interaction. */
int em_roger_tick(EmRoger *, EmRogerStory *, const EmRogerHooks *);
/* Original selector0/class10 radius/height/bearing. Score writes occur
 * after radius success even when a later gate rejects. */
int em_roger_candidate(const float descriptor[2], const float owner[3],
    const EmInteractionPlayer *, const EmInteractionMath *, float *score);
/* The EM_ROGER_TRIGGER hook is 001B1EA0(0, &D_00810350, 0x82AB80, 4):
 * em_director_original_001B1EA0_bound (em_area11_roger.c, census L22). */
#endif
