/* Canonical AREA11 pickup binding (docs/PICKUP_OWNERS.md). The AREA11
 * interaction host binds every placed pickup at load and ticks each owner
 * at its pool node; the host owns Use arbitration (00184BA0) and the
 * 001798D0 pose reset. */
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
/* Whole-world teardown (001AF8E0 frees the owners): every binding is
 * released; the instances, the inventory and the taken bits stay. */
void em_pickup_original_unbind_all(void);
/* Stable token for em_interaction_runtime_claim and live canonical bytes. */
EmPickupOwner *em_pickup_original_owner(uint16_t uid);
/* Advance bound owners in original publication rank order. Status frames
 * must pass ordinary_tasks_enabled0. Returns1 success,-1 retained fault.
 * Player worker and general Use scan run earlier in the same frame. */
int em_pickup_original_tick(float player_y, uint8_t action, uint8_t no_grab,
                             uint8_t scripted_frame, int ordinary_tasks_enabled);
/* One owner update (00219550 / 0015AFA0 state 1 and later) of the bound
 * owner `uid`, at its pool node: 1 while allocated, 0 on the call that
 * freed it (the node then frees itself, 001AFC10), -1 fault (also for an
 * unbound uid). The caller gates status frames. */
int em_pickup_original_tick_one(uint16_t uid, float player_y, uint8_t action, uint8_t no_grab,
                                uint8_t scripted_frame);
/* The bound owner's running take program (its skip byte is the canonical
 * 3B91 view) and its +0xB0 position; NULL when `uid` is not bound. */
EmScript *em_pickup_original_script(uint16_t uid);
const float *em_pickup_original_position(uint16_t uid);
int em_pickup_original_active(void);

/* The canonical map bytes D_00810CB8[t] and key bytes D_00810CC3[t]
 * (they overlap the item counts; bytes past D_00810D1F are not held). */
const uint8_t *em_pickup_maps(void);
const uint8_t *em_pickup_keys(void);
/* Original810C60/CA4/CA6 live bytes, initialized0/FF/0 by001AF2C0.
 * These values are state, not derived from item counts. */
void em_pickup_equipment_read(uint8_t *status, uint8_t *primary, uint8_t *secondary);
void em_pickup_equipment_write(uint8_t status, uint8_t primary, uint8_t secondary);
#endif
