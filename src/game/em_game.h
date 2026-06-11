/* em_game.h — the game task: the engine's slot-0 task chain as native C.
 *
 * Structural translation of the documented chain (FINDINGS.md "ENGINE
 * FRAME ANATOMY"): boot/flow task (func_001AB7E0) -> game task machine
 * (func_001ACEC0) -> sub-machine (func_001AD250) -> in-game frame machine
 * (func_001AE040) -> gameplay frame (func_001AE5E0). See em_game.c for the
 * per-function mapping. Today the gameplay frame drives the port's
 * scene/character rendering, interactive player movement (left stick,
 * camera-relative, through the engine's ANALOG GAIT quantizer —
 * turn-in-place / walk / run — and its 4.5-unit radial wall probes)
 * with an idle<->locomotion crossfade (0.15 s linear palette blend,
 * stride rate-scaled to ground speed) plus the decoded IDLE CYCLE
 * (breathing idle id 0, look-around fidget 349 every 300 frames), and
 * the engine's AUTHENTIC chase camera (struct 0x008101E0 mirror,
 * clamped proportional follow per FINDINGS.md "CAMERA SYSTEM"). The
 * player has NO free camera control (the original gives none): R1/L1
 * orient the camera behind the player, an idle camera slowly
 * auto-orients, and a wall behind the camera makes it RISE instead of
 * pulling in (em_game.c "CAMERA FIDELITY"). While the status screen is
 * open the world simulation PAUSES (gate on em_hud_is_open()). Real
 * game logic replaces the skeleton arms as the decomp repo recovers
 * it. Per-scene boot config (player spawn, collision filename,
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

/* RUNTIME SCENE SWITCH — the native area/sub-state loader slice
 * (FINDINGS "AREA TRANSITION LIFECYCLE" s22, the B8==1 commit's
 * load-at-black sequence). Frees the ACTIVE scene (level meshes,
 * collision world, door + enemy actors — the engine's actor-pool free)
 * and reloads everything from `dir`'s scene manifest (level parts,
 * collision, doors, enemies). The PLAYER model, the BGM stream and the
 * sfx registry PERSIST (the shipped goto links are intra-area
 * sub-state moves — room-move audio semantics: no fade, no restart).
 * `dir` with no '/' resolves as a SIBLING of the current scene dir
 * (manifest goto tails name sibling dirs). The caller places the
 * player afterwards (the spawn-table placement is the transition's,
 * not the scene's). Returns 0 on success; on failure nothing is torn
 * down. Normally consumed from em_door_goto_pending() while the
 * screen is fully black (an invisible cut, like the engine's loader).
 * NOTE: under main.c's EM_SCENE staging the default scene dir name
 * maps to the staged override — sibling goto targets still resolve,
 * but a link BACK to the default-named dir lands on the staged scene. */
int em_game_scene_switch(const char *dir);

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
 * em_game_anim_hold_restart is the FIRE-RECOIL variant (decoded s25,
 * FINDINGS "FIRE ANIM MECHANISM"): the engine plays NO separate fire
 * clip — while the player carries a firing stance code (+0x1F0 in
 * {0x31, 0x34}) the per-bone publisher bone_matrix_publish re-seeds
 * the COMMITTED aim-ladder clip every frame with sample time = the
 * fire counter (+0x276: 0 at every shot, +2/frame). The recoil snap
 * is baked into the FRONT frames of the aim-pose clip itself, so each
 * shot replays the clip from frame 0 and it settles back into the
 * clamped hold. Natively: if `clip_id` is already the committed hold,
 * rewind its playhead to frame 0 at `rate` (no re-request — the
 * engine's counter write, not a mailbox transaction); otherwise it
 * degrades to em_game_anim_hold.
 *
 * em_game_anim_frames returns the frame count of `clip_id` in the
 * loaded player EMDL (0 = model not loaded / clip absent) — the honest
 * clip-length source for state windows that gate on an anim (the
 * weapon draw/reload/holster timers).
 *
 * em_game_anim_frame returns the committed clip's playhead in frames
 * (clamped to the last frame; -1 = no committed clip). It reports the
 * time of the NEXT actor_update evaluation (the commit advances after
 * evaluating) — introspection for the self-tests. */
int      em_game_anim_request(unsigned clip_id, float rate);
int      em_game_anim_hold(unsigned clip_id, float rate);
int      em_game_anim_hold_restart(unsigned clip_id, float rate);
int      em_game_anim_frames(unsigned clip_id);
int      em_game_anim_frame(void);
void     em_game_anim_cancel(void);
unsigned em_game_anim_active(void);

/* MANUAL AIM STEER state (decoded 2026-06-11, func_0017ABA0 — the
 * player aim blends +0x278/+0x27C; full decode in em_game.c's "MANUAL
 * AIM STEER" block). While the armed stance is held, the left stick
 * (d-pad merged) steers:
 *   em_game_aim_pitch — the PITCH blend (+0x278): 0.5 center, 1 = full
 *     up, 0 = full down; INVERTED Y (stick up aims DOWN — the original
 *     behavior). It selects/blends the 0x112..0x11A aim-pose ladder,
 *     so the fire/laser ray (the posed hand bone) follows it.
 *   em_game_aim_yaw_blend — the YAW blend (+0x27C): 0.5 center, the
 *     pose pans +-60 deg before overflow turns the body.
 *   em_game_aim_dir — the world-space aim ray the blends select (the
 *     native equivalent of the engine's gun+0xC0 hand-matrix read).
 * Outside the armed stance the blends rest at their last values; they
 * re-center to 0.5 at every stance entry. */
float em_game_aim_pitch(void);
float em_game_aim_yaw_blend(void);
void  em_game_aim_dir(float out[3]);

#ifdef __cplusplus
}
#endif

#endif /* EM_GAME_H */
