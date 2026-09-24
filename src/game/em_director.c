/* em_director.c — cinematic director and its scripted beats.
 *
 * The scripted-cinematic layer: the beat table and its step/zone/finish
 * helpers, the per-frame director tick (which runs BEFORE the actor update so a
 * cue that moves an actor lands the same frame), the camera override, and the
 * letterbox ramp. Split out of em_game.c, which had grown to hold the entire
 * gameplay frame. Behaviour is unchanged by the move.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_director.h"
#include "game/em_area11_flow.h"
#include "game/em_director_original.h" /* 001C4760, bound over the canonical storage */

#include "game/em_game_internal.h"
/* the director gates on the damage lock and seeds the camera */
#include "game/em_player_damage.h"
#include "game/em_camera.h"
#include "game/em_frame.h"
#include "game/em_scene_bindings.h" /* em_scene_state(): D_00810813, D_00810CC3 */

/* Camera-program approximation retained pending the original script VM.
 * Trigger selection is recovered independently in em_area11_flow.c from
 * runtime AREA11 addresses (the old splat labels were shifted by 0x40).
 * This table still omits script commands: move-to readiness, mode/fade
 * entry, event-counter waits, message cues and teardown. Its keyframe
 * durations alone are not an exact execution timeline. */
static const CineBeat kCineBeats[3] = {
    /* ---- BEAT 0 (step 0x00 -> 0x10): the vault-wheel sweep ---------- */
    {
        335.0f, 385.0f, 228.0f, 255.0f,   /* zone XZ */
        260.0f, 280.0f,                   /* Y gate [260,280] */
        0, CINE_STEP_BEAT1, 1,            /* no op0C line; -> 0x10; register key */
        15, {
            { 0, 300, {356.0f,305.1f,209.4f}, {320.0f,303.5f,180.5f} },
            { 0, 300, {281.0f,331.0f,171.0f}, {315.0f,305.5f,191.5f} },
            { 0, 420, {395.0f,277.1f,272.4f}, {360.0f,284.5f,243.5f} },
            { 0, 300, {281.0f,331.0f,171.0f}, {315.0f,305.5f,191.5f} },
            { 0, 270, {325.0f,317.0f,310.0f}, {334.0f,300.0f,267.5f} },
            { 1, 100, {379.0f,344.8f,275.5f}, {397.0f,307.0f,254.0f} },
            {-1,  45, {379.0f,344.8f,275.5f}, {397.0f,307.0f,254.0f} },
            { 1, 120, {351.0f,296.0f,295.5f}, {396.0f,286.0f,306.0f} },
            {-1,  42, {351.0f,296.0f,295.5f}, {396.0f,286.0f,306.0f} },
            { 1, 120, {476.0f,356.8f,325.9f}, {481.0f,322.2f,294.0f} },
            {-1,  42, {476.0f,356.8f,325.9f}, {481.0f,322.2f,294.0f} },
            { 1, 100, {380.0f,359.0f,237.0f}, {381.0f,320.0f,211.0f} },
            {-1,  42, {380.0f,359.0f,237.0f}, {381.0f,320.0f,211.0f} },
            { 1, 100, {309.0f,306.0f,235.0f}, {324.0f,307.0f,191.0f} },
            {-1,  80, {309.0f,306.0f,235.0f}, {324.0f,307.0f,191.0f} },
        }
    },
    /* ---- BEAT 1 (step 0x10 -> 0x20): op0C message line 0x97 -------- */
    {
        452.0f, 500.0f, 278.0f, 292.0f,   /* zone XZ */
        275.0f, INFINITY,                /* Y gate: playerY >= 275 */
        CINE_MUSIC_BEAT1, CINE_STEP_BEAT2, 0,
        4, {
            { 0,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            { 1, 100, {449.0f,366.0f,269.0f}, {465.0f,325.0f,254.0f} },
            { 1,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            {-1,  90, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
        }
    },
    /* ---- BEAT 2 (step 0x20 -> 0xFF): op0C message line 0x99 -------- */
    {
        410.0f, 439.0f, 175.0f, 203.0f,   /* zone XZ */
        285.0f, INFINITY,                /* Y gate: playerY >= 285 */
        CINE_MUSIC_BEAT2, CINE_STEP_DONE, 0,
        1, {
            /* sub0 +0xC = 0 -> a one-shot hard cut; hold one frame so the
             * cut is visible before exit. */
            { 0,   1, {460.8f,324.5f,201.0f}, {420.0f,303.9f,191.5f} },
        }
    },
};

/* Map the persistent step byte to the active beat index (or -1 = the
 * director is parked: pre-beat-0 is index 0, done = -1). */
int cine_step_to_beat(uint8_t step)
{
    return em_area11_beat_for_step(step);
}

/* The persistent beat step D_00810813 (D_008107D8[0x3B]): the canonical
 * progress byte (em_scene_state.h; migrated from g.cine_step). This
 * stand-in writes it only where 008253F0 does, at a beat completion. */
static uint8_t *step_byte(void)
{
    return em_scene_progress_at(em_scene_state(), 0x00810813u, 1);
}

static EmArea11Triggers s_triggers;
static char s_trigger_path[sizeof g.scene_dir + 32];
static int s_trigger_loaded;

static int director_load_triggers(void)
{
    char path[sizeof s_trigger_path];
    /* The legacy cinematic harness runs on its own fixture scene. */
    snprintf(path, sizeof path, "%s/area11_flow.emaf",
             g.cine_test ? "assets/scene_snow" : g.scene_dir);
    if (strcmp(path, s_trigger_path)) {
        snprintf(s_trigger_path, sizeof s_trigger_path, "%s", path);
        s_trigger_loaded = em_area11_triggers_load(&s_triggers,path);
        if (s_trigger_loaded)
            printf("director: loaded original AREA11 trigger polygons from %s\n",path);
        else if (g.cine_test || strstr(g.scene_dir,"snow"))
            fprintf(stderr,"director: AREA11 triggers unavailable: %s; "
                    "run tools/export_area11_flow.py\n",path);
    }
    return s_trigger_loaded;
}

int cine_in_zone(const CineBeat *beat)
{
    int index = -1;
    for (int i = 0; i < 3; ++i)
        if (beat == &kCineBeats[i]) index = i;
    return index >= 0 && director_load_triggers() &&
           em_area11_trigger_contains(&s_triggers,index,g.pos);
}

/* End the running beat CLEANLY: drop the lock, advance the step byte,
 * fire the on-completion actions (key-item + milestone), and clear the
 * transient so no soft-lock can persist. cine_was stays set for one frame
 * so director_camera does its one-shot chase restore. */
void cine_beat_finish(void)
{
    const CineBeat *b = &kCineBeats[g.cine_beat];
    uint8_t *step = step_byte();
    if (!step) {
        fprintf(stderr, "director: D_00810813 is not canonical storage\n");
        em_frame_request_quit();
        return;
    }
    *step = em_area11_step_after_beat(g.cine_beat);
    if (b->reg_keyitem && em_director_original_001C4760_scene(em_scene_state(), 1, 1) < 0) {
        /* Beat 0's completion stores the step (0x8255D0) and then calls
         * 001C4760(1, 1) (0x8255CC..0x8255D4; em_director_original.h):
         * D_00810CC3[1] += 1 on the canonical key byte (the translation
         * this module's successor em_director_original runs). */
        fprintf(stderr, "director: 001C4760(1, 1) reached a byte the port does not hold\n");
        em_frame_request_quit();
        return;
    }
    g.cine_active = 0;
    g.cine_beat   = -1;
    printf("director: beat done — D_00810813 = %#x\n", *step);
}

/* DIRECTOR TICK — run each gameplay frame BEFORE actor_update (so the
 * lock is set before player_move reads em_game_player_interact_busy) and
 * before camera_update (which reads cine_active/the keyframe state for
 * the override). Tests the current step's zone; on entry starts the
 * beat (raises the lock + letterbox, seats the first keyframe); while
 * running, advances the keyframes by their durations and finishes the
 * beat when they run out.
 *
 * SAFETY: dormant unless a zone is entered. Never starts a beat while
 * any OTHER lock owns the player (door transit, examine, elevator,
 * damage) so the cinematics never collide. cine_active only ever clears
 * via cine_beat_finish (keyframes exhausted) — there is no path that
 * leaves it raised with no advancing program. */
void director_tick(void)
{
    /* OUTSIDE A BEAT: poll the current step's zone. The director is
     * fully dormant here — no camera/lock side effects. */
    if (!g.cine_active) {
        /* don't arm if any other system owns the player this frame */
        if (em_door_movement_locked() || em_examine_input_locked() ||
            em_game_player_interact_busy() || player_damage_locked())
            return;
        const uint8_t *step = step_byte();
        if (!step) {                           /* fail-stop: no canonical byte */
            fprintf(stderr, "director: D_00810813 is not canonical storage\n");
            em_frame_request_quit();
            return;
        }
        int beat = cine_step_to_beat(*step);
        if (beat < 0) return;                  /* director done (0xFF) */
        const CineBeat *b = &kCineBeats[beat];
        if (!cine_in_zone(b)) return;          /* not in the trigger zone */

        /* ENTER the beat (op07): raise lock + letterbox, seat keyframe 0.
         * (The op0C message line is not yet presented; WP-8/WP-10.) */
        g.cine_active = 1;
        g.cine_beat   = beat;
        g.cine_kf     = 0;
        g.cine_kf_t   = 0;
        /* a BLEND first keyframe starts from the live camera; seed the
         * blend start from the current actual eye/tgt. */
        memcpy(g.cine_blend_eye, g.cam.eye, sizeof g.cine_blend_eye);
        memcpy(g.cine_blend_tgt, g.cam.tgt, sizeof g.cine_blend_tgt);
        if (b->music) {
            /* 0x97/0x99 are NOT sound ids: op0C sub0 is the message op
             * 001B7D60 (request kind 2, line word rec+0x14 -> a text line
             * plus a VOICE.DAT cue). No sound is played here; the message
             * and voice push arrive with the script host (WP-8/WP-10). */
            printf("director: beat %d op0C message line %#x (not played; "
                   "WP-8/WP-10)\n", beat, b->music);
        }
        printf("director: beat %d START — zone X[%.0f,%.0f] Z[%.0f,%.0f] "
               "@ (%.1f,%.1f,%.1f) — letterbox + player lock\n", beat,
               b->x0, b->x1, b->z0, b->z1, g.pos[0], g.pos[1], g.pos[2]);
        return;
    }

    /* RUNNING: advance the keyframe program. */
    const CineBeat *b = &kCineBeats[g.cine_beat];
    g.cine_kf_t++;
    if (g.cine_kf_t >= b->key[g.cine_kf].dur) {
        /* advance to the next keyframe; seed a blend's live start from
         * the keyframe we are leaving (its destination = the current
         * framing). */
        g.cine_kf++;
        g.cine_kf_t = 0;
        if (g.cine_kf >= b->n_key) {
            cine_beat_finish();          /* program exhausted -> done */
            return;
        }
        const CineKey *prev = &b->key[g.cine_kf - 1];
        if (prev->blend >= 0) {
            memcpy(g.cine_blend_eye, prev->eye, sizeof g.cine_blend_eye);
            memcpy(g.cine_blend_tgt, prev->tgt, sizeof g.cine_blend_tgt);
        }
        /* else (leaving a WAIT) the blend start carries unchanged */
    }
}

/* DIRECTOR CAMERA OVERRIDE — called from camera_update inside the
 * top_mode-0 block, BEFORE the door/examine cues, so a running beat
 * owns the camera outright. Writes cam->eye/tgt (and the desired
 * mirrors) from the current keyframe: a CUT snaps and holds; a BLEND
 * lerps from the keyframe's live start toward its destination across
 * its duration; a WAIT holds the prior framing. Returns 1 while it owns
 * the camera. On the frame the beat ends (cine_was set, cine_active
 * cleared) it does a ONE-SHOT chase restore behind the unmoved player
 * (the op18 / op07-sub4 restore — exactly the examcam release shape) and
 * returns 0 so the normal chase resumes cleanly. */
int director_camera(EmCamera *cam)
{
    if (g.cine_active) {
        const CineBeat *b = &kCineBeats[g.cine_beat];
        const CineKey  *k = &b->key[g.cine_kf];
        float eye[3], tgt[3];
        if (k->blend == 1 && k->dur > 0) {
            /* lerp from the live start toward the destination */
            float t = (float)g.cine_kf_t / (float)k->dur;
            if (t > 1.0f) t = 1.0f;
            for (int i = 0; i < 3; i++) {
                eye[i] = g.cine_blend_eye[i] +
                         (k->eye[i] - g.cine_blend_eye[i]) * t;
                tgt[i] = g.cine_blend_tgt[i] +
                         (k->tgt[i] - g.cine_blend_tgt[i]) * t;
            }
        } else {
            /* CUT or WAIT: hold the keyframe's framing exactly */
            memcpy(eye, k->eye, sizeof eye);
            memcpy(tgt, k->tgt, sizeof tgt);
        }
        memcpy(cam->eye_des, eye, sizeof eye);
        memcpy(cam->tgt_des, tgt, sizeof tgt);
        memcpy(cam->eye, eye, sizeof eye);
        memcpy(cam->tgt, tgt, sizeof tgt);
        cam->tgt_soft = 0;
        g.cine_was = 1;
        return 1;
    }
    if (g.cine_was) {
        /* RESTORE (op18 / op07 sub4): one-shot chase re-seat behind the
         * unmoved player, then hand the camera back to the normal solve
         * (identical to the examcam release / locked-door finish). */
        g.cine_was = 0;
        cam->tgt_soft = 0;
        cam->tgt_des[0] = g.pos[0];
        cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
        cam->tgt_des[2] = g.pos[2];
        camera_desired_eye(cam);
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
    }
    return 0;
}

/* Letterbox alpha for this frame (0 = no bars). Raised while a beat
 * runs (cine_fade ramps up to CINE_BAR_FADE), dropped when it ends
 * (ramps back to 0) — a minimal fade in/out around the s81 ~64-line
 * top/bottom black bars. Called from frame_close_out. FLAGGED: the
 * exact opening staggered-fade frame counts are un-stopwatched (doc
 * §F); CINE_BAR_FADE is an approximation. */
float director_letterbox_alpha(void)
{
    if (g.cine_active) {
        if (g.cine_fade < CINE_BAR_FADE) g.cine_fade++;
    } else {
        if (g.cine_fade > 0) g.cine_fade--;
    }
    if (g.cine_fade <= 0) return 0.0f;
    return (float)g.cine_fade / (float)CINE_BAR_FADE;   /* 0..1 */
}

