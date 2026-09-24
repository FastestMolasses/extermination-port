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

/* Publication (001B17A0 / 001B1B70) and the interactive list's store are the
 * collision world's since census L07 (em_owner_services_001B17A0 and
 * em_collision_world.h); the host fills `list.active` from that published
 * list before a scan (its published_view).
 *
 * Refreshes status/class flags from the canonical live owners, then runs
 * the list. candidate->owner is EmInteractionSceneOwner*; its native_owner
 * field is the host controller binding. */
int em_interaction_scene_scan(EmInteractionScene *scene,EmInteractionScanState *state,
                              EmInteractionPredicate predicate,void *context,
                              size_t *winner_index);
/* Native predicates may return-1 for a missing required worker. This
 * wrapper retains original valid-result ordering and score behavior, but
 * commits no owner arm or scan state when any evaluated predicate fails.
 * A failed native service is never an eligible original actor. */
int em_interaction_scene_scan_checked(EmInteractionScene *scene, EmInteractionScanState *state,
    EmInteractionPredicate predicate, void *context, size_t *winner_index);
#endif
