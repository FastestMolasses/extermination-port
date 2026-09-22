/* Validated local EMIS metadata and canonical-owner bindings. */
#ifndef EM_INTERACTION_SCENE_H
#define EM_INTERACTION_SCENE_H
#include "game/em_interaction_scan.h"

typedef enum {
    EM_INTERACTION_PICKUP,EM_INTERACTION_PANEL,EM_INTERACTION_ELEVATOR,
    EM_INTERACTION_DOOR,EM_INTERACTION_ROGER
} EmInteractionRole;

typedef struct {
    uint32_t source_id,role,publication_rank,callback,item_type;
    uint16_t uid;
    uint8_t initial_status,class_flags,subtype,selector;
    float descriptor[6],position[3],angles[3];
    void *native_owner;
    uint8_t *live_status,*live_class_flags,*live_armed;
} EmInteractionSceneOwner;

typedef struct {
    EmInteractionMath math;
    EmInteractionSceneOwner owners[EM_INTERACTION_CAPACITY];
    size_t count;
    EmInteractionList list;
    char error[128];
} EmInteractionScene;

/* Clears previous metadata/bindings. Returns1 only on a complete valid
 * EMIS v1 AREA11 file; failure leaves count0 and a concrete error. */
int em_interaction_scene_load(EmInteractionScene *scene,const char *path);
EmInteractionSceneOwner *em_interaction_scene_find(EmInteractionScene *scene,uint32_t source_id);
EmInteractionSceneOwner *em_interaction_scene_pickup(EmInteractionScene *scene,uint16_t uid);
/* Unique non-pickup role lookup; NULL for a missing/nonunique role. */
EmInteractionSceneOwner *em_interaction_scene_role(EmInteractionScene *scene,EmInteractionRole role);

/* Bind every live owner to its real controller bytes before it can be
 * published. source_id is stable across process allocation/re-entry.
 * A removed owner should clear its live interactive flag; do not leave
 * dangling pointers in a previously published list. */
int em_interaction_scene_bind(EmInteractionScene *scene,uint32_t source_id,void *native_owner,
                              uint8_t *status,uint8_t *class_flags,uint8_t *armed);

/* Invoke at that owner's original B1B70 publication point, in original
 * active-walker order. position is its current culling point (door uses
 * model origin+10Y). The caller preserves controller/story early-outs.
 * 1 published,0 culled/noninteractive,-1 required owner binding missing.
 * Missing visible Roger/door bindings are concrete failures, never an
 * implicit removal from competition. */
int em_interaction_scene_offer(EmInteractionScene *scene,uint32_t source_id,
                               const float position[3],const float camera_anchor[3],
                               const float camera_forward[3]);
void em_interaction_scene_publish(EmInteractionScene *scene);

/* Refreshes status/class flags from the canonical live owners, then runs
 * the previous-frame list. candidate->owner is EmInteractionSceneOwner*;
 * its native_owner field is the host controller binding. */
int em_interaction_scene_scan(EmInteractionScene *scene,EmInteractionScanState *state,
                              EmInteractionPredicate predicate,void *context,
                              size_t *winner_index);
#endif
