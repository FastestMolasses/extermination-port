/* AREA01's composition over existing live owners. No original behavior or
 * resource bytes are implemented here. Unresolved workers remain failures. */
#ifndef EM_AREA01_LIVE_H
#define EM_AREA01_LIVE_H
#include "game/em_area01_runtime.h"
#include "game/em_area01_actor_view.h"
#include "game/em_area01_player_view.h"
#include "game/em_area01_model_live.h"
#include "game/em_area01_model_draw.h"
#include "game/em_area01_door_live.h"
#include "game/em_area01_collision_view.h"
#include "game/em_area01_script_live.h"
#include "game/em_area01_pickup_live.h"
#include "game/em_area01_interaction_live.h"
#include "game/em_owner_services_original.h"
#include "game/em_area01_state.h"
#include "game/em_module_loader.h"

typedef struct {
    void *ctx;
    EmActorPool *pool;
    EmSceneState *scene;
    EmArea01State *area;
    EmModuleLoader *loader;
    uint32_t *current_actor;
    const uint64_t *resource_epoch;
    /* Remaining canonical memory and worker adapters. A worker runs with
     * byte views active. Before native actor/player access it must suspend
     * and then resume through the public transaction functions below. */
    uint8_t *(*bytes)(void *, uint32_t, uint32_t, int);
    int (*worker)(void *, EmArea01Call *);
    int (*rebind)(void *, EmActor *, uint32_t);
    int (*private_model)(void *, const EmActor *);
} EmArea01LiveHost;

typedef struct {
    EmArea01LiveHost host;
    EmArea01Runtime runtime;
    EmArea01ActorView actors;
    EmArea01PlayerView player;
    EmArea01Model model;
    EmArea01ModelDraw draw;
    EmArea01Door door;
    EmArea01CollisionView collision;
    EmArea01Script scripts;
    EmArea01Pickup pickups;
    EmArea01Interaction interaction;
    /* Projections are observations, never writable model storage. */
    uint32_t metadata[3], slots[EM_OWNER_SERVICES_MAX_BONES];
    /* Shared actor construction matrix. The existing pose/draw scratch
     * owners begin at 3400; none owns 3000..303F. Original 001029C0 fills
     * this matrix before C3BE0/C3D60 or the class-2 owner consumes it. */
    uint32_t scratch_3000[16];
    /* The second and third 001E3D90 matrices. The existing aim owner
     * supplies 36A0; no earlier native owner supplies 36E0..375F.
     * 00102958 writes each complete matrix before its first reader. */
    uint32_t scratch_36E0[32];
    /* 001C3DB0's product matrix 34C0..34FF: its first 001026D0 writes all
     * four rows before the second reads them back (exitb f_1C3DB0). The
     * draw owner stops at 34BF; the player closure's 001C9D50 words are
     * its own write-first storage on the player stage. */
    uint32_t scratch_34C0[16];
    /* 001CD2B0 writes its integer screen-distance result before the flame
     * caller reads it. This word has no earlier live storage owner. */
    uint32_t d275C00;
    /* Private call locals replace the EE stack. Not persistent game RAM. */
    uint8_t locals[0x8000];
    uint32_t fault_address;
    int fault, bound, active, player_stage;
} EmArea01Live;

/* After pool reset, player pose attachment, delivered banks, shared scratch
 * and interaction services. May bind before the first 0015C420 reservation:
 * slot count/head are borrowed pointers. Binding allocates no model/slots
 * and resets no pool, player or overlay. */
int em_area01_live_bind(EmArea01Live *, const EmArea01LiveHost *);
/* After the shared script host drops its borrowed descriptors at reset. */
void em_area01_live_detach(EmArea01Live *);
int em_area01_live_resume(EmArea01Live *);
int em_area01_live_suspend(EmArea01Live *);
uint8_t *em_area01_live_bytes(EmArea01Live *, uint32_t, uint32_t, int);
/* Exact active canonical matrix span, also available to native adapters.
 * Returns an alias of scratch_3000; never publishes a copied matrix. */
uint8_t *em_area01_live_matrix_3000(EmArea01Live *, uint32_t, uint32_t);
/* Head sprite owner views for em_effects_live (outside a transaction):
 * an AREA01 private-model owner's canonical record image, and its slot
 * arena records. 0 / non-NULL, or -1 / NULL. */
int em_area01_live_head_record(EmArea01Live *, uint32_t address, uint8_t record[0x2F0]);
const uint8_t *em_area01_live_head_bytes(EmArea01Live *, uint32_t address, uint32_t size);
/* Outermost call only. Nested translated calls use runtime_call directly. */
int em_area01_live_call(EmArea01Live *, EmArea01Call *);
/* A scene service that already owns the active transaction may dispatch
 * a nested original caller using the same call-local stack. */
int em_area01_live_call_active(EmArea01Live *, EmArea01Call *);
/* The Use dispatch's scan inside the existing player stage. Its actual
 * in-stage record is borrowed read-only, without post-stage projections. */
int em_area01_live_scan(EmArea01Live *,EmPlayerLiveActor *,int *result);
#endif
