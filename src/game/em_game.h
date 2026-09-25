/* em_game.h — the game task: the engine's slot-0 task chain as native C.
 *
 * Structural translation of the documented chain (FINDINGS.md "ENGINE
 * FRAME ANATOMY"): boot/flow task (func_001AB7E0 [byte-matched]) ->
 * game task machine (func_001ACEC0 [NEARMISS]) -> sub-machine
 * (func_001AD250 [byte-matched]) -> in-game frame machine ->
 * GAMEPLAY FRAME (func_001AE5E0 [NEARMISS]).
 *
 * PROVENANCE (audit 2026-07-31):
 *   - 0x001AE040 is `anim_frame_top_b`, now recovered as NEARMISS
 *     readable C (src/anim_frame_top_b.c in the decomp repo). It is the
 *     in-game frame machine (task +0xB); its states are recorded in
 *     docs/SCENE_COORDINATOR_DESIGN.md §2.3 from that C, the .s and a
 *     measured trace. The port's ingame_frame_machine does not yet follow
 *     them (WP-3).
 *   - func_001AE5E0 IS the gameplay frame, and the mapping in em_game.c
 *     is source-derived. An EARLIER AUDIT CALLED THIS WRONG ("a
 *     per-level INIT routine ... the port's gameplay_frame is a port
 *     construction"); that verdict is OVERTURNED. src/func_001AE5E0.c
 *     [NEARMISS — logic authoritative] is a 13-call straight line whose
 *     body is exactly the stage list em_game.c documents, arguments
 *     included: bump the frame counter D_00810750 (and 0x70003B68),
 *     func_001CB590(0x008102B0, 0x320, D_008102B9, frame) ->
 *     func_0015BCF0 (player actor update) -> func_001CB5A0 ->
 *     func_001D1C50 (render chain) -> func_001C1D00(0x008101D0) ->
 *     func_001AFD70(0) -> func_0015C160 -> func_001F0360 ->
 *     func_001CB590(0x008101E0, 0xD0, 0, 0) -> func_0018B9C0 (camera
 *     machine) -> func_001CB5A0 -> func_001AAD00 -> func_001D1EA0(1).
 *     Nothing in it is level setup; D_00810750 is a per-frame counter
 *     (it is bumped on every call and handed to the first
 *     func_001CB590 as its 4th argument).
 * See em_game.c for the per-function mapping. Today the port's frame
 * drives the port's
 * scene/character rendering, interactive player movement (left stick,
 * camera-relative, through the engine's ANALOG GAIT quantizer —
 * turn-in-place / walk / run — and its 4.5-unit radial wall probes)
 * with an idle<->locomotion crossfade (0.15 s linear palette blend,
 * stride rate-scaled to ground speed) plus the decoded IDLE CYCLE
 * (breathing idle id 0, look-around fidget 349 every 300 frames —
 * re-confirmed by audit against func_00161020 [NEARMISS]: +0x28 =
 * 0x12C and func_001749A0(self, 0x15D, 1, 8.0f) are literal there;
 * CORRECTED 2026-07-31 — the same case-1 sub-0 branch gates the whole
 * countdown on the LOW-HEALTH latch `!(+0x235 & 1)`, so a player at
 * health <= 35 never fidgets, only breathes: see em_game.c "IDLE
 * CYCLE"),
 * and
 * the engine's AUTHENTIC chase camera (struct 0x008101E0 mirror,
 * clamped proportional follow per FINDINGS.md "CAMERA SYSTEM"). The
 * player has NO free camera control (the original gives none): R1/L1
 * orient the camera behind the player, an idle camera slowly
 * auto-orients, and a wall behind the camera makes it RISE instead of
 * pulling in (em_game.c "CAMERA FIDELITY"). While the status screen is
 * open the world is frozen (frame-machine state 3, S11b). Real
 * game logic replaces the skeleton arms as the decomp repo recovers
 * it. Per-scene boot config (player spawn, collision filename) comes
 * from the SCENE MANIFEST assets/scene/scene.txt (exporter-written;
 * missing = office defaults). A manifest `bgm` key is accepted and
 * IGNORED: no original code starts music from scene data (area music is
 * 001FAE70's cue choice on the stream lanes, em_stream_live).
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

/* New-game handoff after the original intro movie. Uses func_001AF2C0's
 * player defaults and area 11.0 instead of the debug save-state fixture. */
void em_game_install_new(void);

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
 * the same mailbox). CONFIRMED by audit against src/func_00183090.c
 * [byte-matched — hand-written asm that assembles to the original
 * words, so it is authoritative]: the per-frame COMMIT compares +0x1F2
 * with +0x20C, and only when they DIFFER stores +0x1F2 -> +0x20C,
 * calls anim_clip_init(actor, +0x20C, +0x1F8, 0.0f) and then clears
 * the anim flag word +0x200 (the clear is AFTER the call, not before;
 * immaterial natively). Equal ids return with nothing done. The id IS
 * the container index in the actor's bound clip
 * library (player: chunk28/f01_id3c — anim id == EMDL clip-table id).
 * Same function shows the SCRIPTED path: +0x2F3 == 1 or 3 re-seeds the
 * pose with bone_init_default_2(actor, +0x1F2), zeroes +0x200 and
 * advances +0x2F3 to 2 or 4 respectively, bypassing the compare
 * entirely — that is the scripted-anim ownership handshake the
 * interact lock below models. TIGHTENED (audit 2026-07-31): every
 * OTHER nonzero +0x2F3 — i.e. the advanced states 2 and 4 the re-seed
 * itself writes — returns immediately WITHOUT committing, so the
 * mailbox stays frozen for the whole scripted window, not just its
 * first frame. Only +0x2F3 == 0 reaches the ordinary id compare.
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
 * FINDINGS "FIRE ANIM MECHANISM"; the re-seed half CONFIRMED by audit
 * against bone_matrix_publish [byte-matched]): the engine plays NO
 * separate fire clip — while the player carries a firing stance code
 * (+0x1F0 in {0x31, 0x34}) the per-bone publisher bone_matrix_publish
 * re-seeds the COMMITTED aim-ladder clip every frame with sample time
 * = the fire counter, literally
 *   anim_clip_arbiter(obj, id, 0.0f, (float)*(short *)(obj + 0x276)).
 * The counter's own "+2/frame, 0 at every shot" cadence is NOT in that
 * function and stays OBSERVED. The recoil snap
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

/* MANUAL AIM STEER state (func_0017ABA0 [NEARMISS] — the player aim
 * blends +0x278/+0x27C; full decode, and one audit CORRECTION to the
 * R2 rate table, in em_game_internal.h's "MANUAL AIM STEER" block).
 * While the armed stance is held, the left stick (d-pad merged)
 * steers:
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

/* AREA-11 PROGRESSION.
 *
 * D_00810811 is NOT a battery flag. It is the opening-complete byte: the
 *   AREA11 opening controller 00823E80 stores 0xFF there at
 *   0x00823F74..80 when its script ends (g.opening_complete; executed by
 *   tools/test_continue_reset_reference.py). The byte is also index 0xB9
 *   of the D_00810758 event array, so event writes could reach it; that
 *   has not been audited. No pickup take writes it, and there is no
 *   battery accessor here.
 *
 * em_game_terminal_powered — bit 7 of the per-area power byte
 *   D_00810841[D_00810700] (D_0081084C for AREA11), read from the
 *   canonical D2 progress region (em_scene_state.h, migrated in WP-4). In
 *   the original it is set by 001580C0 (1 << panel +0x2E, sound 0x3EE),
 *   the panel program's record callback, which the AREA11 interaction
 *   host binds; the terminal owner 00827B10 tests it to pick its powered
 *   script 0x82A750 over the refusal 0x82A990. 001AF2C0's memset clears
 *   it (em_scene_progress_reset_001AF2C0). The legacy elevator ride and
 *   the scripted interact-clip lock that used to live here were retired
 *   in WP-4 with the examine terminal: the ride is the original carry
 *   00828050 inside the host's elevator program. */
int  em_game_terminal_powered(void);

/* em_game_player_interact_busy — 1 while the opening runtime or an
 *   acquired original player source (the interaction host's 0015B130
 *   takeover) owns the player, so
 *   the legacy use scans (examine, doors) do not start a second
 *   interaction while one is in flight. (The legacy interact-clip lock of
 *   the legacy pickup take was deleted in WP-6.)
 *
 * em_game_player_face_step — the examine op04 FACE pre-roll. Turn the
 *   player body heading toward `target_yaw` by one standing turn-in-place
 *   step (TURN_IP_GAIT03 = 0.39269909 rad = 22.5 deg/frame,
 *   SNAP-when-within) and return 1 once the player is facing it
 *   (snapped), else 0 (still turning). PROVENANCE SPLIT (audit): the
 *   STEPPER is source-derived — func_00174AC0 [NEARMISS] drives the body
 *   heading through func_001B12B0(goal, cur, rate) and picks 0.39269909f
 *   for the standing gait-0/3 case. That the EXAMINE op04 pre-roll reuses
 *   that particular rate is OBSERVED: the cited
 *   INVESTIGATION_examine_walk_face.md exists in neither repo, and no
 *   recovered examine-script function names it. The examine sequence
 *   (em_examine.c) calls this each frame while its input lock holds, so
 *   the FACE pivot plays out before the message. Does NOT touch
 *   player_move's desired-heading / movement-v3 path — the examine lock
 *   already suppresses free locomotion for the script window. */
int  em_game_player_interact_busy(void);
int  em_game_player_face_step(float target_yaw);

#ifdef __cplusplus
}
#endif

#endif /* EM_GAME_H */
