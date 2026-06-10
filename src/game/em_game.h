/* em_game.h — the game task: the engine's slot-0 task chain as native C.
 *
 * Structural translation of the documented chain (FINDINGS.md "ENGINE
 * FRAME ANATOMY"): boot/flow task (func_001AB7E0) -> game task machine
 * (func_001ACEC0) -> sub-machine (func_001AD250) -> in-game frame machine
 * (func_001AE040) -> gameplay frame (func_001AE5E0). See em_game.c for the
 * per-function mapping. Today the gameplay frame drives the port's
 * scene/character rendering, interactive player movement (left stick,
 * camera-relative) with an idle<->walk animation crossfade (0.15 s linear
 * palette blend, walk stride rate-scaled to ground speed) and a lerped
 * chase camera (d-pad orbits it); real game logic replaces the skeleton
 * arms as the decomp repo recovers it.
 */
#ifndef EM_GAME_H
#define EM_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Register the boot task into task slot 0 — the native counterpart of the
 * engine init's func_001AB740(0, 0x001AB7E0). Call after em_frame_init();
 * the boot task runs on the first em_task_dispatch, loads assets, then
 * replaces itself with the game task. */
void em_game_install(void);

/* Release everything the game loaded (GPU meshes, models). Call after
 * em_frame_run() returns, before the gfx device is destroyed. */
void em_game_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_GAME_H */
