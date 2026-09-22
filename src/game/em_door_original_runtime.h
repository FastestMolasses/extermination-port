/* Canonical AREA11 door resources and original passive pose publication.
 * The scene supplies real script/transition and draw/publication workers. */
#ifndef EM_DOOR_ORIGINAL_RUNTIME_H
#define EM_DOOR_ORIGINAL_RUNTIME_H

#include "em_model.h"
#include "game/em_door_original.h"
#include "game/em_interaction_scene.h"
#include "game/em_pose_bank.h"

typedef struct {
    void *context;
    int (*kickoff)(void *, int locked);
    int (*script_tick)(void *);
    int (*script_start)(void *, uint32_t entry);
    int (*transition)(void *);
    int (*publish)(void *, const float position[3]);
    int (*draw)(void *);
} EmDoorOriginalRuntimeHooks;

typedef struct {
    EmDoorOriginal owner;
    EmModel model;
    EmPoseBank bank;
    EmPosePlayback playback;
    EmDoorOriginalRuntimeHooks hooks;
    uint32_t source_id;
    uint8_t destination[4];
    uint16_t sounds[2];
    float angles[3], descriptor[2], matrix[16], palette[48];
    int loaded, failed;
    const char *error;
} EmDoorOriginalRuntime;

/* Load only source0082A3C0 and cross-check EMIS against original EMDO.
 * The original initialization callback is completed here, before ordinary
 * publication begins. Output palette has two original nodes plus identity.
 * GPU upload and character-light context belong to the caller; the model
 * carries original normals. Callers must never draw its identity placeholder. */
int em_door_original_runtime_load(EmDoorOriginalRuntime *, const char *scene_dir,
    const EmInteractionSceneOwner *, const EmDoorOriginalRuntimeHooks *);
int em_door_original_runtime_tick(EmDoorOriginalRuntime *, uint8_t transition_pending);
/* Bind owner.status/class_flags/armed and runtime itself to the EMIS source.
 * A canonical scan sets the armed bit; this helper supports direct callers. */
int em_door_original_runtime_arm(EmDoorOriginalRuntime *);
/* Original door command's clip binding must use this raw source bank.
 * The currently recovered worker accepts blend0/start0 only. */
int em_door_original_runtime_animation(EmDoorOriginalRuntime *, unsigned clip);
/* Remove scene bindings and GPU objects before this releases CPU resources. */
void em_door_original_runtime_free(EmDoorOriginalRuntime *);

#endif
