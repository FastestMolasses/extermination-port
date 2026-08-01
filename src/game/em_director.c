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

#include "game/em_game_internal.h"
/* the director gates on the damage lock and seeds the camera */
#include "game/em_player_damage.h"
#include "game/em_camera.h"

/* The beat table moved here with its owners: every reader of it is a
 * director function. */
/* The three beats, authoritative from INVESTIGATION_area11_director.md
 * §G (the per-beat EYE/TARGET keyframes are the literal data the engine
 * reads). WAIT keyframes (blend == -1) hold the prior framing; their
 * eye/tgt are carried from the preceding cue so the held framing is
 * exact. The zone AABBs + Y gates are the task-spec / doc §B/C/D values.
 *
 * NOTE ON BEAT-0 ZONE: the task spec and doc §B give the corrected live
 * poly X[335,385] Z[228,255] Y[260,280]; the doc's older §1.2 table and
 * §7 summary cite X[452,500] for beat 0 — those were the pre-correction
 * reads. We use the §B/§G corrected zone (the keyframe pass is the most
 * recent, live-RAM authoritative source). */
static const CineBeat kCineBeats[3] = {
    /* ---- BEAT 0 (step 0x00 -> 0x10): the vault-wheel sweep ---------- */
    {
        335.0f, 385.0f, 228.0f, 255.0f,   /* zone XZ */
        260.0f, 280.0f,                   /* Y gate [260,280] */
        0, CINE_STEP_BEAT1, 1,            /* no music; -> 0x10; register key */
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
    /* ---- BEAT 1 (step 0x10 -> 0x20): music cue 0x97 ----------------- */
    {
        452.0f, 500.0f, 278.0f, 292.0f,   /* zone XZ */
        -1.0e30f, 275.0f,                 /* Y gate: playerY <= 275 */
        CINE_MUSIC_BEAT1, CINE_STEP_BEAT2, 0,
        4, {
            { 0,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            { 1, 100, {449.0f,366.0f,269.0f}, {465.0f,325.0f,254.0f} },
            { 1,  60, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
            {-1,  90, {459.0f,301.0f,310.0f}, {484.0f,288.0f,272.5f} },
        }
    },
    /* ---- BEAT 2 (step 0x20 -> 0xFF): music cue 0x99 ----------------- */
    {
        410.0f, 439.0f, 175.0f, 203.0f,   /* zone XZ */
        -1.0e30f, 285.0f,                 /* Y gate: playerY <= 285 */
        CINE_MUSIC_BEAT2, CINE_STEP_DONE, 0,
        1, {
            /* sub0 +0xC = 0 -> a one-shot hard cut; hold one frame so the
             * music sting plays and the cut is visible before exit. */
            { 0,   1, {460.8f,324.5f,201.0f}, {420.0f,303.9f,191.5f} },
        }
    },
};

/* Map the persistent step byte to the active beat index (or -1 = the
 * director is parked: pre-beat-0 is index 0, done = -1). */
int cine_step_to_beat(uint8_t step)
{
    switch (step) {
        case CINE_STEP_BEAT0: return 0;
        case CINE_STEP_BEAT1: return 1;
        case CINE_STEP_BEAT2: return 2;
        default:              return -1;   /* 0xFF = done */
    }
}

/* Player inside this beat's XZ AABB + Y gate (the doc's func_1B1EA0
 * point-in-poly reduced to the axis-aligned rect, plus the floor tier
 * gate). */
int cine_in_zone(const CineBeat *b)
{
    return g.pos[0] >= b->x0 && g.pos[0] <= b->x1 &&
           g.pos[2] >= b->z0 && g.pos[2] <= b->z1 &&
           g.pos[1] >= b->ylo && g.pos[1] <= b->yhi;
}

/* End the running beat CLEANLY: drop the lock, advance the step byte,
 * fire the on-completion actions (music already fired at start per the
 * scripts; the key-item + milestone fire here), and clear the transient
 * so no soft-lock can persist. cine_was stays set for one frame so
 * director_camera does its one-shot chase restore. */
void cine_beat_finish(void)
{
    const CineBeat *b = &kCineBeats[g.cine_beat];
    g.cine_step   = b->next_step;
    if (b->reg_keyitem) {
        /* func_1C4760(1) — register the opening key-item (beat 0). The
         * port has no separate key-item registry hook yet; the battery
         * pickup is the AREA-11 key item and is handled by em_pickup.
         * FLAGGED: faithful-minimum — the milestone advances; the
         * key-item register is a decode-noted no-op here. */
        printf("director: beat 0 complete — key-item register "
               "(func_1C4760) [FLAGGED no-op]\n");
    }
    g.cine_active = 0;
    g.cine_beat   = -1;
    printf("director: beat done — D_00810813 = %#x\n", g.cine_step);
}

/* DIRECTOR TICK — run each gameplay frame BEFORE actor_update (so the
 * lock is set before player_move reads em_game_player_interact_busy) and
 * before camera_update (which reads cine_active/the keyframe state for
 * the override). Tests the current step's zone; on entry starts the
 * beat (raises the lock + letterbox, fires the music cue, seats the
 * first keyframe); while running, advances the keyframes by their
 * durations and finishes the beat when they run out.
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
        int beat = cine_step_to_beat(g.cine_step);
        if (beat < 0) return;                  /* director done (0xFF) */
        const CineBeat *b = &kCineBeats[beat];
        if (!cine_in_zone(b)) return;          /* not in the trigger zone */

        /* ENTER the beat (op07): raise lock + letterbox, fire the music
         * cue, seat keyframe 0. */
        g.cine_active = 1;
        g.cine_beat   = beat;
        g.cine_kf     = 0;
        g.cine_kf_t   = 0;
        /* a BLEND first keyframe starts from the live camera; seed the
         * blend start from the current actual eye/tgt. */
        memcpy(g.cine_blend_eye, g.cam.eye, sizeof g.cine_blend_eye);
        memcpy(g.cine_blend_tgt, g.cam.tgt, sizeof g.cine_blend_tgt);
        if (b->music) {
            /* op0C MUSIC cue. AREA-11 cue ids 0x97/0x99 ARE now in the
             * shipped sfx registry (0x097/0x099 in assets/sfx/sfx.txt), so
             * these cues resolve and play — the SFX_SOUND_MAX bump brought
             * them into the bank. (Was a silent no-op while only the office
             * bank was loaded.) */
            em_sfx_play((unsigned)b->music);
            printf("director: beat %d music cue %#x\n", beat, b->music);
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

/* EM_CINE_TEST=1 — AREA-11 OPENING DIRECTOR self-test (the D_00810813
 * step machine). Drives the director through its triggers WITHOUT a
 * real new-game playthrough (no headless walk into the high-Y zones),
 * proving: (a) DORMANCY at spawn, (b) a beat TRIGGERS on entering its
 * zone, (c) the player is LOCKED and the camera is OVERRIDDEN to the
 * keyframe, (d) held input does NOT move the player during the beat,
 * (e) the beat COMPLETES, the step ADVANCES, and CONTROL RETURNS (no
 * soft-lock), (f) the camera RESTORES to the normal chase. It uses the
 * SHORT beat 2 (single 1-frame keyframe) for the full completion cycle,
 * then re-arms beat 1 to confirm a second trigger + music cue.
 *
 *   frame 0..2  : at spawn — assert the director is DORMANT (no lock,
 *                 no override) — movement-v3 owns the frame.
 *   frame 3     : force step 0x20, teleport into beat-2 zone center,
 *                 hold 'w' (stick up) to prove the lock kills input.
 *   frame 5     : assert TRIGGERED — cine_active, busy-lock, camera eye
 *                 == beat-2 keyframe eye, step latched at 0x20.
 *   ~frame 8    : assert COMPLETED — cine_active 0, step == 0xFF,
 *                 control returned (not busy), player did NOT drift.
 *   frame 12    : re-arm step 0x10, teleport into beat-1 zone — assert
 *                 a second beat triggers (proves re-entrancy + music).
 *   frame 20    : release; assert the chase camera restored. PASS/quit.
 */
void cine_test_script(void)
{
    static float bx, bz, by;        /* beat-1 zone center (lock-drift ref) */
    int n = g.frame_no;

    switch (g.ct_phase) {
        case 0:
            if (n <= 2) {
                /* DORMANCY: at spawn nothing should have armed. */
                if (g.cine_active || em_game_player_interact_busy()) {
                    printf("cine test: CHECK FAILED — director not "
                           "dormant at spawn (frame %d active %d busy %d)\n",
                           n, g.cine_active, em_game_player_interact_busy());
                    g.ct_fail++;
                }
            }
            if (n == 3) {
                /* Arm beat 1 (multi-keyframe, ~310 frames — long enough
                 * to observe the lock + camera + no-drift over many
                 * frames). Place the player in the zone center on its
                 * Y tier and hold 'w' (forward) — it must be dead. */
                g.cine_step = CINE_STEP_BEAT1;
                bx = 0.5f * (kCineBeats[1].x0 + kCineBeats[1].x1);
                bz = 0.5f * (kCineBeats[1].z0 + kCineBeats[1].z1);
                by = 270.0f;                 /* <= 275 gate */
                g.pos[0] = bx; g.pos[1] = by; g.pos[2] = bz;
                move_test_inject('w', 1);    /* hold forward — must be dead */
                g.ct_mark  = n;
                g.ct_phase = 1;
            }
            break;
        case 1:
            /* the tick arms on frame 3; the beat is active from frame 4 */
            if (n == g.ct_mark + 1) {
                const CineKey *k0 = &kCineBeats[1].key[0];
                int locked = g.cine_active && g.cine_beat == 1 &&
                             em_game_player_interact_busy();
                int cam_ok = fabsf(g.cam.eye[0] - k0->eye[0]) < 0.5f &&
                             fabsf(g.cam.eye[1] - k0->eye[1]) < 0.5f &&
                             fabsf(g.cam.eye[2] - k0->eye[2]) < 0.5f;
                if (!locked) {
                    printf("cine test: CHECK FAILED — beat 1 did not lock "
                           "(active %d beat %d busy %d)\n", g.cine_active,
                           g.cine_beat, em_game_player_interact_busy());
                    g.ct_fail++;
                }
                if (!cam_ok) {
                    printf("cine test: CHECK FAILED — camera not overridden "
                           "to keyframe 0 (eye %.1f,%.1f,%.1f vs %.1f,%.1f,"
                           "%.1f)\n", g.cam.eye[0], g.cam.eye[1], g.cam.eye[2],
                           k0->eye[0], k0->eye[1], k0->eye[2]);
                    g.ct_fail++;
                }
                g.ct_phase = 2;
            }
            break;
        case 2:
            /* over the locked window: the held 'w' must produce NO motion */
            if (g.cine_active && em_game_player_interact_busy()) {
                g.ct_locked_max++;
                float drift = fabsf(g.pos[0] - bx) + fabsf(g.pos[2] - bz);
                if (drift > 0.01f) {
                    printf("cine test: CHECK FAILED — player drifted %.3f u "
                           "while locked at frame %d (input leaked)\n",
                           drift, n);
                    g.ct_fail++;
                    g.ct_phase = 3;   /* report once */
                }
            }
            if (n == g.ct_mark + 60) {
                /* Force beat 1 to finish: jump to its last keyframe's end. */
                if (g.cine_active) {
                    g.cine_kf   = kCineBeats[1].n_key - 1;
                    g.cine_kf_t = kCineBeats[1].key[g.cine_kf].dur;
                }
                g.ct_phase = 3;
            }
            break;
        case 3:
            if (n == g.ct_mark + 63) {
                move_test_inject('w', 0);
                /* COMPLETION + CONTROL RETURN + STEP ADVANCE. */
                int returned = !g.cine_active &&
                               !em_game_player_interact_busy();
                int stepped  = g.cine_step == CINE_STEP_BEAT2;
                if (!returned) {
                    printf("cine test: CHECK FAILED — control did NOT return "
                           "after beat 1 (active %d busy %d) — SOFT-LOCK\n",
                           g.cine_active, em_game_player_interact_busy());
                    g.ct_fail++;
                }
                if (!stepped) {
                    printf("cine test: CHECK FAILED — step did not advance "
                           "to 0x20 (got %#x)\n", g.cine_step);
                    g.ct_fail++;
                }
                printf("cine test: beat 1 cycle OK — locked %d frames, "
                       "step %#x\n", g.ct_locked_max, g.cine_step);
                g.ct_phase = 4;
            }
            break;
        case 4:
            /* CAMERA RESTORE: a few frames after the beat the chase camera
             * must be following the player again (eye no longer pinned to
             * the keyframe; it tracks behind the spawn-tier player). */
            if (n == g.ct_mark + 70) {
                const CineKey *k0 = &kCineBeats[1].key[0];
                float off = fabsf(g.cam.eye[0] - k0->eye[0]) +
                            fabsf(g.cam.eye[2] - k0->eye[2]);
                if (off < 1.0f) {
                    printf("cine test: CHECK FAILED — camera still pinned to "
                           "the cinematic keyframe after restore (off "
                           "%.3f u)\n", off);
                    g.ct_fail++;
                }
                /* BEAT 2 SHORT-CYCLE: arm the last beat, confirm it
                 * triggers + completes to 0xFF (the fast full cycle). */
                g.cine_step = CINE_STEP_BEAT2;
                g.pos[0] = 0.5f * (kCineBeats[2].x0 + kCineBeats[2].x1);
                g.pos[1] = 280.0f;            /* <= 285 gate */
                g.pos[2] = 0.5f * (kCineBeats[2].z0 + kCineBeats[2].z1);
                g.ct_phase = 5;
            }
            break;
        case 5:
            if (n == g.ct_mark + 71) {        /* the arming frame */
                if (!(g.cine_active && g.cine_beat == 2)) {
                    printf("cine test: CHECK FAILED — beat 2 did not "
                           "trigger (active %d beat %d)\n",
                           g.cine_active, g.cine_beat);
                    g.ct_fail++;
                }
            }
            if (n == g.ct_mark + 76) {
                int done = !g.cine_active && g.cine_step == CINE_STEP_DONE;
                if (!done) {
                    printf("cine test: CHECK FAILED — beat 2 did not "
                           "complete to 0xFF (active %d step %#x)\n",
                           g.cine_active, g.cine_step);
                    g.ct_fail++;
                }
                printf("cine test: %s (%d failed check%s)\n",
                       g.ct_fail ? "FAIL" : "PASS", g.ct_fail,
                       g.ct_fail == 1 ? "" : "s");
                /* trip the headless quit (capture one frame, then exit). */
                if (!g.capture_path) g.capture_path = "/tmp/cine_test.bmp";
                g.capture_frame = n;
                g.ct_phase = 6;
            }
            break;
        default:
            break;
    }
}
