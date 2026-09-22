/* Scene adapter for AREA11's elevator owner, original script and shared
 * interaction player. The ordinary player stage remains a separate call. */
#ifndef EM_ELEVATOR_RUNTIME_H
#define EM_ELEVATOR_RUNTIME_H

#include "game/em_elevator_program.h"
#include "game/em_interaction_runtime.h"

typedef struct {
    void *context;
    int (*align_player)(void *, const float position[3]);
    int (*face_player)(void *, float yaw);
    int (*camera_set)(void *, const float eye[3], const float target[3]);
    int (*camera_publish)(void *);
    int (*camera_chase)(void *); /* Original D/sub5, distinct from panel D/sub3. */
    int (*message_start)(void *, uint32_t token, uint32_t delay);
    int (*message_done)(void *);
    void (*sound)(void *, unsigned cue, float radius);
    void (*rebuild_pose)(void *, float height);
    void (*copy_indicator_pose)(void *);
    void (*update_actor)(void *);
} EmElevatorRuntimeHooks;

typedef struct {
    EmElevator owner;
    EmElevatorMotion motion;
    EmElevatorProgram program;
    EmInteractionRuntime *interaction;
    EmElevatorRuntimeHooks hooks;
    float *player_ground_y; /* Actor+A4, not the animated hip. */
    float *camera_target_y; /* The actual vector published by camera_set. */
    int failed;
} EmElevatorRuntime;

/* Initialize an unused adapter; return1 on success. All world operations
 * must be bound. The adapter and both Y mirrors must remain at stable addresses. */
int em_elevator_runtime_load(EmElevatorRuntime *, const char *program_path,
    int lower, EmInteractionRuntime *, float *player_ground_y,
    float *camera_target_y, const EmElevatorRuntimeHooks *);
/* Called only after the elevator wins the original use scan. Publishes
 * selector3 and armed bit4 together. A competing owner cannot be displaced. */
int em_elevator_runtime_arm(EmElevatorRuntime *);
/* Call once at the ordinary pooled-actor stage. Status frames freeze owner,
 * script and motion together. Power is the live area bit7, never possession. */
int em_elevator_runtime_tick(EmElevatorRuntime *, int powered,
    int ordinary_tasks_enabled);
/* Return0 while the shared player still refers to this adapter. Normal
 * completion releases on the following ordinary player callback. */
int em_elevator_runtime_free(EmElevatorRuntime *);

#endif
