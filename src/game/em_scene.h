/* em_scene.h — scene manifest parse, load and unload.
 *
 * The scene.txt manifest reader and the scene lifetime: parse the manifest,
 * load the parts it names, and tear everything down on a switch. Split out of
 * em_game.c, which had grown to hold the entire gameplay frame. Behaviour is
 * unchanged by the move — only the file boundary is new.
 */
#ifndef EM_SCENE_H
#define EM_SCENE_H

/* The subsystem's shared types and state live here. */
#include "game/em_game_internal.h"

int scene_load(EmGfx *gfx, SceneItem *items, int max_items);
void scene_unload(EmGfx *gfx);
void scene_manifest_load(void);

#endif /* EM_SCENE_H */
