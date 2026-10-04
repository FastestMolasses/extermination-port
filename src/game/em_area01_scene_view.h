/* Direct, bounded views of scene-owned original bytes. No copied storage. */
#ifndef EM_AREA01_SCENE_VIEW_H
#define EM_AREA01_SCENE_VIEW_H
#include "game/em_scene_state.h"
/* NULL for a reserved progress byte, a gap, an empty/wrapped span, or a
 * span crossing separately owned fields. D_008101E4 belongs to the camera
 * and is deliberately absent. Native integer fields require little endian. */
uint8_t *em_area01_scene_view(EmSceneState *scene, uint32_t address, uint32_t size);
#endif
