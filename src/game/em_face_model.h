#ifndef EM_FACE_MODEL_H
#define EM_FACE_MODEL_H
#include "em_model.h"
#include "game/em_opening_face.h"

/* Dynamic face attachment for the verified 21-node model47 skeleton.
 * The body must have no mixed bone7/body triangles. */
typedef struct {
    EmOpeningFace state;
    uint32_t first, count;
    float *deltas, *positions;
} EmFaceModel;

int em_face_model_attach(EmFaceModel *, EmModel *body,
                         const char *mesh_path, const char *morph_path);
void em_face_model_free(EmFaceModel *);
/* BA580 consumes activity1/2 before the existing D0C70 updater. Other
 * activity values survive. RNG must be the shared original service. */
int em_face_model_tick(EmFaceModel *, const EmModel *, uint8_t *activity,
                       EmFaceRandom random, void *context);
#endif
