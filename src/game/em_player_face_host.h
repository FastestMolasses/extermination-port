#ifndef EM_PLAYER_FACE_HOST_H
#define EM_PLAYER_FACE_HOST_H

#include "em_gfx.h"
#include "em_model.h"
#include "game/em_face_model.h"

/* Dennis model3B's cinematic face resources. The ordinary model remains
 * caller-owned and is never modified. model owns independent render data;
 * its baked palette/clip arrays are deliberately absent. The caller draws
 * this mesh with the current original player palette (22 native slots). */
typedef struct {
    EmGfx *gfx;
    EmGfxMesh *mesh;
    EmModel model;
    EmFaceModel face;
    EmFaceRandom random;
    void *random_context;
    uint8_t ready, attached, failed;
} EmPlayerFaceHost;

/* Start with a zeroed host. Resource preparation consumes no RNG and does
 * not attach the face. A failed load preserves the ordinary model. */
int em_player_face_host_load(EmPlayerFaceHost *, EmGfx *, const EmModel *ordinary,
                            const char *scene_dir, EmFaceRandom, void *context);
/* GPU deletion precedes CPU data release. Safe for a zero/partial host. */
void em_player_face_host_free(EmPlayerFaceHost *);

/* B81D0: a fresh face starts from zero; repeated attach preserves current
 * weights and wait fields through D0690's partial reset, then sets speed1.
 * Detach models CA770/AF890's cleared state without altering body geometry. */
int em_player_face_host_attach(EmPlayerFaceHost *);
void em_player_face_host_detach(EmPlayerFaceHost *);

/* FD950's Dennis path invokes D06E0 directly. No Roger activity byte is
 * accepted or consumed. A missing attached face is a concrete failure. */
int em_player_face_host_talk(EmPlayerFaceHost *, uint8_t talking);
/* Invoke once where 83090 calls D0C70, before the body animation stage.
 * The caller controls ordinary/status timing and shared RNG order. A
 * detached prepared host is a successful no-op; failure is sticky. */
int em_player_face_host_tick_before_body(EmPlayerFaceHost *);

/* 1 active alternate mesh, 0 detached, -1 unavailable/failed. No fallback
 * occurs after a required mesh-update failure. model is render-only. */
int em_player_face_host_record(const EmPlayerFaceHost *, EmGfxMesh **,
                              const EmModel **);
const EmOpeningFace *em_player_face_host_state(const EmPlayerFaceHost *);

#endif
