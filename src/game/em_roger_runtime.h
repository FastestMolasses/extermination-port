#ifndef EM_ROGER_RUNTIME_H
#define EM_ROGER_RUNTIME_H
#include "em_gfx.h"
#include "game/em_face_model.h"
#include "game/em_interaction_scene.h"
#include "game/em_player_pose.h"
#include "game/em_roger.h"
#include "game/em_roger_assets.h"

typedef struct EmRogerRuntime EmRogerRuntime;
typedef EmScriptCommandResult (*EmRogerCommand)(void *, EmRogerRuntime *, EmScript *,
                                               const unsigned char *);
typedef struct {
    void *context;
    /* Exact original actor transform, using the current source position and
     * angles. Finite hierarchy multiplication retains its documented tolerance. */
    int (*owner_matrix)(void *, const float position[3], const float angles[3], float matrix[16]);
    /* Perform canonical publication at B17A0 and return original visibility.
     * This precedes the initial branch's forced rendered flag. */
    int (*publish)(void *, EmRogerRuntime *);
    /* STOP_STREAMS, RESUME_MUSIC, FADE_IN, REMOVE_GROUP, RELEASE_FACE and FREE.
     * Native pose/face/draw events are consumed by the runtime itself. */
    int (*event)(void *, EmRogerRuntime *, EmRogerEvent, unsigned argument);
    /* Original programs, through the real shared scene services. No handler
     * may simulate success for an unsupported subcommand or resource. */
    EmRogerCommand frame;   /*07: shared player/camera frame ownership */
    EmRogerCommand camera;  /*00 */
    EmRogerCommand player;  /*0A */
    EmRogerCommand actor;   /*0B: remaining actor commands; sub4 is native */
    EmRogerCommand message; /*15: message/voice command */
    EmRogerCommand scene;   /*remaining stream/story/fade/wait;01/sub0,10 native */
    EmFaceRandom random;    /*shared original RNG, using context */
} EmRogerRuntimeHooks;

struct EmRogerRuntime {
    EmRoger owner;
    EmRogerAssets assets;
    EmPoseBank encounter;
    EmFaceModel face;
    EmPlayerPose pose;
    EmScript script;
    EmRogerStory *story;
    uint8_t *face_activity; /*shared8106D5; messages publish1/2 */
    const EmInteractionMath *math;
    EmRogerRuntimeHooks hooks;
    EmGfx *gfx;
    EmGfxMesh *mesh;
    float position[3], angles[3], descriptor[2], player[3];
    float owner_matrix[16], palette[22*16];
    uint16_t bank;
    int ready, failed, draw_pending;
};

/* Loads into a fresh/freed runtime: scene_dir/roger plus opening/Roger face.
 * Metadata is the canonical source82A500 record, never a later capture.
 * Caller must keep story, activity and math alive. Initial clip8 starts at0;
 * the controller consumes no ordinary callback during loading. */
int em_roger_runtime_load(EmRogerRuntime *, EmGfx *, const char *scene_dir,
    const EmInteractionSceneOwner *, const EmInteractionMath *, EmRogerStory *,
    uint8_t *face_activity, const EmRogerRuntimeHooks *);
void em_roger_runtime_free(EmRogerRuntime *);
/* Hook into the actual pooled owner walker. Suppressed/status frames advance
 * neither animation nor RNG. 1 live,0 skipped/freed,-1 required worker fault. */
int em_roger_runtime_tick(EmRogerRuntime *, const float player[3], int ordinary);
/* Register &runtime->owner status/class_flags/armed with the scene, using
 * runtime itself as the stable native_owner token. Original script writes
 * must use the placement setter so culling, Use and rendering share position. */
int em_roger_runtime_set_placement(EmRogerRuntime *, const float position[3], const float angles[3]);
int em_roger_runtime_animation(EmRogerRuntime *, uint16_t bank, uint16_t clip,
                                unsigned blend, float source_frame);
/* One source controller DRAW publication. Hidden0, ready1, fault-1. */
int em_roger_runtime_record(const EmRogerRuntime *, EmGfxMesh **,
                            const float **world_palette, uint32_t *bones);
const EmModel *em_roger_runtime_model(const EmRogerRuntime *);
#endif
