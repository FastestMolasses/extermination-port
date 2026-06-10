/* em_game.h — the game task: the engine's slot-0 task chain as native C.
 *
 * Structural translation of the documented chain (FINDINGS.md "ENGINE
 * FRAME ANATOMY"): boot/flow task (func_001AB7E0) -> game task machine
 * (func_001ACEC0) -> sub-machine (func_001AD250) -> in-game frame machine
 * (func_001AE040) -> gameplay frame (func_001AE5E0). See em_game.c for the
 * per-function mapping. Today the gameplay frame drives the port's
 * scene/character rendering, interactive player movement (left stick,
 * camera-relative) with an idle<->walk animation crossfade (0.15 s linear
 * palette blend, walk stride rate-scaled to ground speed) and the
 * engine's AUTHENTIC chase camera (struct 0x008101E0 mirror, clamped
 * proportional follow per FINDINGS.md "CAMERA SYSTEM"; d-pad feeds its
 * yaw); real game logic replaces the skeleton arms as the decomp repo
 * recovers it. Per-scene boot config (player spawn, collision filename,
 * optional bgm) comes from the SCENE MANIFEST assets/scene/scene.txt
 * (exporter-written; missing = office defaults). EM_BGM=<path.wav> makes
 * the boot->game handoff start looping level music through em_bgm (the
 * engine's func_001FB0B0 BGM model); with neither the manifest bgm key
 * nor the env set, silence — behavior unchanged.
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

/* SCRIPTED PLAYER ANIM — the engine's anim-request mailbox, natively
 * (FINDINGS.md "ANIM ID MAPPING" + "DOOR SCRIPTS DECODED", s23).
 *
 * Engine model: a request writes the clip id halfword to player+0x1F2
 * and the playback rate float to +0x1F8 (door scripts do it through
 * op 0x0A sub 0; the weapon arbiters and the locomotion defaults use
 * the same mailbox). The per-frame COMMIT (func_00183090) copies
 * +0x1F2 -> +0x20C when they differ and calls anim_clip_init(actor,
 * id, rate): the id IS the container index in the actor's bound clip
 * library (player: chunk28/f01_id3c — anim id == EMDL clip-table id).
 *
 * Natively: em_game_anim_request latches the request; actor_update
 * commits it on its next run (the engine's own one-frame request ->
 * commit latency), suspends the idle<->walk locomotion blend, and
 * plays the clip ONCE at `rate` frames/tick, holding the last frame
 * until it ends — then locomotion resumes by itself. Returns 1 if the
 * loaded player EMDL carries `clip_id`, else 0 (no state change; the
 * caller decides how to degrade).
 *
 * em_game_anim_cancel is the script-teardown reset (the op 0x18 end
 * marker / mode-exit family writes +0x1F2 = 0): clears request +
 * committed state, locomotion resumes on the next actor_update.
 *
 * em_game_anim_active returns the committed clip id (0 = none) — the
 * native +0x20C, for the door sequence and the self-tests.
 *
 * em_game_anim_hold is the HELD-POSE variant (the weapon system's aim
 * pose): same request mailbox, but the committed clip CLAMPS at its
 * last frame and KEEPS owning the palette — the engine analog is the
 * armed-stance tops re-selecting the same aim-pose id through the
 * arbiter every frame (FINDINGS "ANIM ID MAPPING": the commit only
 * fires on an id CHANGE, so the pose persists for as long as the state
 * keeps requesting it). The hold ends on the next em_game_anim_request
 * / em_game_anim_hold of a different id, or em_game_anim_cancel.
 *
 * em_game_anim_frames returns the frame count of `clip_id` in the
 * loaded player EMDL (0 = model not loaded / clip absent) — the honest
 * clip-length source for state windows that gate on an anim (the
 * weapon draw/reload/holster timers). */
int      em_game_anim_request(unsigned clip_id, float rate);
int      em_game_anim_hold(unsigned clip_id, float rate);
int      em_game_anim_frames(unsigned clip_id);
void     em_game_anim_cancel(void);
unsigned em_game_anim_active(void);

#ifdef __cplusplus
}
#endif

#endif /* EM_GAME_H */
