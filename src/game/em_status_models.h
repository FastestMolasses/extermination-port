/* The status hub's 3D models (WP-5): the static actor pool D_0028B020 that
 * 0020CDC0 fills and walks, bound to the translated owners of
 * em_status_scene_original (001AFF10/001AFF90/001AFEB0/001AFE60/001B0000,
 * 0020E250/0020E1E0/0020E460, 0020E6F0/0020EC80/001F4BF0), and the draws
 * their +0x4C method 001CB580 queues. Docs: docs/STATUS_SCENE.md section 7.
 *
 * Worker bindings (each names the original it stands for):
 *   001AF7C0 / 001AF800   the bone-slot pop and push. The port has no
 *                         D_00275BD0/D_00275BCC stack (em_area11_bindings.h
 *                         convention: every slot request is available); the
 *                         slots are this module's own EmOwnerBone records.
 *   001C6120, 001CA6E0, 001C6150   model lookup and bind over the exported
 *                         models (tools/export_status_models.py): the menu
 *                         player D_0028A57C and the glyph models of the
 *                         library D_0028A56C that the status-hub capture
 *                         binds. Any other model faults.
 *   001C63E0, 001C67E0, 001C64F0   the player bank D_0028A580 clips 0x1C2 and
 *                         0x0A through em_player_pose (the original channel
 *                         evaluator, docs/PLAYER_POSE.md).
 *   001C62C0, 001C6380, 001029C0, 00102B08/BB0/A60   em_owner_services_original.
 *   001026D0, 001026A0    the VU0 row transform (same forms as 001C9610).
 *   001C69A0              the animated bone pose: per bone quat_to_mat3 of the
 *                         evaluated channels, rows scaled by the channel scale,
 *                         then 001C9610's rest x animation x parent chain over
 *                         the object matrix scaled by +0x60.
 *   001D2040              the GS state packet channel 0: 1 before each draw
 *                         (TEST 0x5000D, Z write), 0 after.
 *   001CB580              the draw: 001CB4F0 with lighting mode 1 (001D8C20(1)
 *                         -> 001D89D0 -> 001D8C30 case 1: no light directions,
 *                         no light colours, ambient row = 128 + actor +0x80..
 *                         +0x88 through the 8388608 bias), the fog set +0x100
 *                         (001D2830(0, 0) while ctx+0xC bit 0 is set, as in the
 *                         status-hub capture: F clamps at 255, no fog), and the
 *                         node matrices +0x90 as the palette.
 *   001CD520              the D_008104E4 == 1 glow sprite: not translated,
 *                         faults (D_008104E4 is 0 in every first-level route
 *                         capture 00..14).
 *
 * Fail-stop: any fault latches; every later call returns -1. */
#ifndef EM_STATUS_MODELS_H
#define EM_STATUS_MODELS_H

#include <stdint.h>

#include "em_gfx.h"
#include "game/em_status_hub.h"
#include "game/em_status_scene_original.h"

typedef struct EmStatusModels EmStatusModels;

/* The globals the owners read, as the port holds them. */
typedef struct {
    float health;       /* D_00810858 */
    float infection;    /* D_0081085C */
    uint8_t d8104E4;    /* D_008104E4 */
    uint8_t d810C60;    /* D_00810C60 */
    uint8_t ca[4];      /* D_00810CA4..D_00810CA7 */
} EmStatusModelsInputs;

/* Load the exported models from `directory` (assets/status_models). NULL
 * when an asset is missing or invalid (reported). */
EmStatusModels *em_status_models_load(const char *directory);
/* Frees the models; gfx (may be NULL) releases the GPU meshes. */
void em_status_models_free(EmStatusModels *models, EmGfx *gfx);

/* 0020DFA0's D_00810610 writes: 001029C0(D_00810610), D_00810624 *= -1. */
int em_status_models_configure(EmStatusModels *models);
/* 001AFEB0 (bone release of every record in use) and 001AFE60 (pool clear). */
int em_status_models_release(EmStatusModels *models);
int em_status_models_clear(EmStatusModels *models);
/* 0020CDC0's status-model workers: EM_STATUS_HUB_INSTALL_DRAW (argument
 * 0x0020E6F0: 001AFF10, then +0x10 = the argument), EM_STATUS_HUB_BUILD_MODELS
 * (0020E250) and EM_STATUS_HUB_ACTORS_TICK (001B0000). 1 accepted, -1 fault. */
int em_status_models_event(EmStatusModels *models, EmStatusHubEvent event, unsigned argument,
                           const EmStatusModelsInputs *inputs);
/* Draw what the last 001B0000 walk queued (and forget it), on 0020DFA0's
 * camera: the GS projection for `zoom` (ctx+0x2468) over D_00810610. 1, or
 * -1. */
int em_status_models_render(EmStatusModels *models, EmGfx *gfx, float zoom);

/* For tests: the draws the last walk queued, the pool, and the node world
 * matrices (+0x90, original row layout) of a record's bones. */
unsigned em_status_models_queued(const EmStatusModels *models);
/* Model draws handed to em_gfx since the models were loaded. */
unsigned long em_status_models_drawn(const EmStatusModels *models);
const EmStatusScenePool *em_status_models_pool(const EmStatusModels *models);
int em_status_models_node_world(const EmStatusModels *models, unsigned record, unsigned bone,
                                float out[16]);
const EmStatusSceneFault *em_status_models_fault(const EmStatusModels *models);

/* The 001C69A0 composition over explicit bone channel fields, for the
 * capture check: object = the 0x70003400 matrix before its +0x60 scaling.
 * node i: translation (+0x00), scale (+0x18), rotation (the quat_nlerp result
 * of +0x30/+0x40/+0x50), parent (+0x64), rest angles (+0x70), rest
 * translation (+0x7C), rest scale (+0x88). world[i] receives +0x90. 0 or -1. */
typedef struct {
    float translation[3], scale[3], rotation[4];
    int16_t parent;
    float rest_rot[3], rest_trans[3];
    int16_t rest_scale[3];
} EmStatusModelsNode;
int em_status_models_pose_001C69A0(const float object[16], const float scale[4],
                                   const EmStatusModelsNode *nodes, unsigned count,
                                   float (*world)[16]);

#endif
