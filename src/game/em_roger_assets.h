#ifndef EM_ROGER_ASSETS_H
#define EM_ROGER_ASSETS_H
#include "em_model.h"
#include "game/em_pose_bank.h"
#include "game/em_script.h"

typedef struct {
    EmModel model;
    EmPoseBank animation;
    EmScriptImage programs;
    float trigger[4][4];
} EmRogerAssets;

/* Model47, default bank4A, original four programs and polygon. There is
 * no fallback pose or generated trigger volume. The caller owns GPU data. */
int em_roger_assets_load(EmRogerAssets *, const char *directory);
void em_roger_assets_free(EmRogerAssets *);
#endif
