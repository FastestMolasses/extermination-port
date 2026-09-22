/* Canonical AREA11 pickup binding. Independent legacy scans are disabled
 * once this adapter is enabled. The game owns Use arbitration/pose reset. */
#ifndef EM_PICKUP_ORIGINAL_H
#define EM_PICKUP_ORIGINAL_H
#include "game/em_pickup_owner.h"
#include "game/em_interaction_scene.h"
#include "game/em_interaction_runtime.h"

typedef struct {
    void *context;
    /* Original1B7F90 and1B8FC0 workers. -1 failure,0 waiting,1 done.
     * The camera worker receives the command phase, not an invented timer. */
    int (*turn)(void *, uint32_t source_id, const float position[3], float step);
    int (*camera)(void *, uint32_t source_id, const float position[3], EmScript *);
    /* Publishes original B0/B1 after the inventory mutation. The next
     * outer frame opens status before the script's terminal release. */
    int (*status_request)(void *, uint8_t kind, uint8_t index);
    /* Original owner event boundary; see em_pickup_owner.h. Publication
     * returns actual render visibility and publishes the canonical owner
     * where eligible. DRAW only marks its deferred draw record visible.
     * FREE/PERSIST/STOP_CHILD are handled by the adapter itself. */
    int (*event)(void *, uint32_t source_id, EmPickupOwnerEvent, uint32_t argument);
} EmPickupOriginalHooks;

/* Bind after original metadata and render instances load, before any Use
 * scan.1 bound,-2 already taken (no live render instance),0 invalid.
 * script_path is pickup_0015afa0.emsc or pickup_00219550.emsc as appropriate. */
int em_pickup_original_bind(const EmInteractionSceneOwner *, EmInteractionRuntime *,
                             const char *script_path, const EmPickupOriginalHooks *);
/* Stable token for em_interaction_runtime_claim and live canonical bytes. */
EmPickupOwner *em_pickup_original_owner(uint16_t uid);
/* Advance bound owners in original publication rank order. Status frames
 * must pass ordinary_tasks_enabled0. Returns1 success,-1 retained fault.
 * Player worker and general Use scan run earlier in the same frame. */
int em_pickup_original_tick(float player_y, uint8_t action, uint8_t no_grab,
                             uint8_t scripted_frame, int ordinary_tasks_enabled);
int em_pickup_original_active(void);

const uint8_t *em_pickup_maps(void);
const uint8_t *em_pickup_keys(void);
/* Original810C60/CA4/CA6 live bytes, initialized0/FF/0 by001AF2C0.
 * These values are state, not derived from item counts. */
void em_pickup_equipment_read(uint8_t *status, uint8_t *primary, uint8_t *secondary);
void em_pickup_equipment_write(uint8_t status, uint8_t primary, uint8_t secondary);
#endif
