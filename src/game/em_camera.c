/* em_camera.c — camera system (chase solve, modes, commit).
 *
 * The engine's camera: the chase/follow solve and its three per-shape solvers,
 * the per-mode dispatch (fixed director cameras, the over-shoulder aim mode, door
 * cinematics), and the commit that publishes eye/target for the frame. Split out
 * of em_game.c, which had grown to hold the entire gameplay frame. Behaviour is
 * unchanged by the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_camera.h"

#include "game/em_game_internal.h"

/* func_0018C6A0(src, dst, max) — the engine's HORIZONTAL chase
 * primitive, one axis: clamped proportional step. d = src - dst;
 * |d| <= 1.0 -> quarter-step snap (d/4); else move |d|/6 capped at max.
 * Speed-limited exponential follow — no splines. */
static float cam_chase_h(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 6.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

/* func_0018C4B0(vec, target_y, max) — the VERTICAL twin: divisor 8. */
static float cam_chase_v(float dst, float src, float max)
{
    float d = src - dst;
    if (fabsf(d) <= 1.0f) return dst + d * 0.25f;
    float step = fabsf(d) / 8.0f;
    if (step > max) step = max;
    return dst + (d > 0.0f ? step : -step);
}

/* Yaw-anchored desired eye: CAM_DIST behind the player along the
 * struct yaw, CAM_EYE_HEIGHT up. As of the s76 idle-emergence pass
 * this is NO LONGER the idle path (idle now runs the emergent tether +
 * solver + entry seat — see the main dispatch's follow branch and
 * camera_entry_seat). It remains the placement shape for the doorcam /
 * examine-restore re-seats and the auto-orbit / L1 yaw seek
 * (cam_yaw_blocked), where a fixed yaw IS authoritative — porting
 * those to the emergent path is future work. */
void camera_desired_eye(EmCamera *cam)
{
    cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * CAM_DIST;
    cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
    cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * CAM_DIST;
}

/* func_001B0080 — the ENTRY SEAT (scene-load camera placement, s76
 * decode; RE-VERIFIED against src/func_001B0080.c, byte-matched: the
 * non-special arm sets target = player pos with +0x24 += 17.0f, eye =
 * target + rotY(spad 3B50 euler)*(0,0,cam+0x0C) with +0x14 += fparg0,
 * then hard-copies both to D_008105E0/D_008105D0): the eye hard-seats |cam+0x0C| = 46.8 BEHIND the SPAWN yaw
 * (the per-record camdist, g.cam_dist_param, NOT the CAM_DIST=33
 * stand-in) at player.y + 19, looking at the player at player.y + 17.
 * At the AREA-11 opening this lands the eye IN A WALL; the follow
 * solver (func_0018DD20) then pulls it in / shoves it laterally / and
 * the dead-band tether freezes the result — the idle camera is
 * EMERGENT from this seat, not a fixed yaw-anchored pose. The struct
 * yaw is the entry sight-line heading (= the spawn yaw, an output of
 * the placement).
 *
 * Seats behind cam->yaw (set to the live spawn facing g.yaw at the
 * memset/spawn — which equals g.spawn_yaw for a real scene load and
 * tracks any test-harness yaw override; using g.spawn_yaw directly
 * would ignore the door/pause/move-test spawn overrides). */
void camera_entry_seat(EmCamera *cam)
{
    /* OPENING-CAMERA SEAT (scene.txt `opencam`, s79 it4): when the scene
     * declares the engine's emergent opening pose, seat it DIRECTLY — eye
     * x/z at the settled +X position, eye.y at the frame-0 seat height
     * (ey0), target at the pinned look-at. This reproduces the live
     * frame-0 read (eye at +X immediately, y not yet raised); the idle
     * follow branch then seeks eye.y up to the settled ey1. Used because
     * the byte-faithful func_0018DD20 cannot relocate the wall-buried
     * spawn-yaw seat across to +X (collision-geometry-driven; struct
     * EmGameState.opencam_on doc + FINDINGS "AREA-11 OPENING CAMERA"). */
    if (g.opencam_on) {
        cam->eye_des[0] = g.opencam_eye[0];
        cam->eye_des[1] = g.opencam_ey0;
        cam->eye_des[2] = g.opencam_eye[2];
        cam->tgt_des[0] = g.opencam_tgt[0];
        cam->tgt_des[1] = g.opencam_tgt[1];
        cam->tgt_des[2] = g.opencam_tgt[2];
        cam->yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                          cam->tgt_des[2] - cam->eye_des[2]);
        cam->swing = 0;
        g.opencam_idle = 0;
        return;
    }
    float follow = fabsf(g.cam_dist_param);   /* |cam+0x0C| = 46.8 */
    cam->tgt_des[0] = g.pos[0];
    cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
    cam->tgt_des[2] = g.pos[2];
    cam->eye_des[0] = g.pos[0] - sinf(cam->yaw) * follow;
    cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
    cam->eye_des[2] = g.pos[2] - cosf(cam->yaw) * follow;
    cam->yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                      cam->tgt_des[2] - cam->eye_des[2]);
    cam->swing = 0;
}

/* func_00191390 — the camera pre-step (DECODED s65; s71 STATE-ID
 * CORRECTION). RE-VERIFIED against src/func_00191390.c (byte-matched,
 * .word form; decoded by hand): sw zero,0x94 / sw zero,0x98, dispatch
 * on lw a1,0x230(a1); codes 2/4/0xF -> 0x8C=-3.0, 0x5C=1.0; codes
 * 6/7/8/9/0x2C/0x2D -> 0.0/2.0; code 0x13 -> 11.0/2.0; codes 1/3 and
 * every other code fall into the cam+0x64 test (== -31.2 -> 2.0/6.0,
 * else 6.0/2.0); tail lb 0x6D, non-zero -> sw 23.0 to +0x98.
 * It zeroes the per-frame extras (+0x94/+0x98) and writes
 * the PER-STATE height params +0x8C/+0x5C every frame. The -3.0/1.0
 * row matches player states 2/4/0xF ONLY — the +0x236 ELEVATED/hang
 * family (the func_0015CBA0 state map picks 2-vs-1 and 4-vs-3 on
 * that latch), NOT ordinary locomotion: a ground walk is state 3 =
 * the idle/default row, so the camera KEEPS eye +19 / target +17
 * while the player moves (PCSX2-verified — the s67 low-ride binding
 * dived the camera toward the player's feet on every step and is
 * retired). The port models no +0x236, so the row is constant:
 * 6.0 / 2.0 (the -31.2 areas natively swap to 2.0 / 6.0 — same sums,
 * +0x64 not carried). The climb family (0/2.0) and state 0x13
 * (11.0/2.0) have no native states. +0x6D != 0 -> +0x98 = 23.0 is
 * unfed (the +0x6D writer is the solver pre-pass func_0018D330). */
static void camera_prestep_00191390(EmCamera *cam)
{
    cam->aim_h  = CAM_AIM_OFFSET;     /* +0x8C = 6.0 */
    cam->var_5c = CAM_IDLE_VAR5C;     /* +0x5C = 2.0 */
}

/* func_00191D40 — the walk camera's desired-EYE-HEIGHT seek (DECODED
 * s65; RE-VERIFIED against src/func_00191D40.c, which writes the eye
 * Y at arg0+0x14 while func_0022FCA0 owns +0x10/+0x18): want = base (+ cam+0x98, unfed) clamped to the y_hi bound;
 * proportional |d|/10 capped at `rate` (4.0 from func_00230000), d/5
 * snap inside 1.0 u. A RISE is vetoed by solver bit 0x80, a DESCENT
 * by bit 0x40 (ceiling-/floor-clamped last solve) — the engine also
 * vetoes descent on the pre-pass sight bit +0x5A & 1 (untranslated).
 * Engine area specials omitted (no native reach): area 0x10 room 1
 * subs 2/4/6 cap the eye at y_lo + 7.5 when y_lo > 100; area 3 room 1
 * x < 356 caps at y_hi - 2 above 250. */
static void cam_eye_y_seek_00191D40(EmCamera *cam, float want, float rate)
{
    if (want > cam->y_hi) want = cam->y_hi;
    float d = want - cam->eye_des[1];
    if (d > 0.0f) {
        if (cam->hit & 0x80) return;
    } else {
        if (cam->hit & 0x40) return;
    }
    if (fabsf(d) <= 1.0f) {
        cam->eye_des[1] += d / 5.0f;
    } else {
        float step = fabsf(d) / 10.0f;
        if (step > rate) step = rate;
        cam->eye_des[1] += d > 0.0f ? step : -step;
    }
}

/* func_0022FCA0 — the walk camera's desired-eye x/z TETHER (DECODED
 * s65 — the full policy in the constants block above; RE-VERIFIED
 * against src/func_0022FCA0.c: err = D_00810690 - fabs(p+0xC), the
 * -20/-10 threshold on p+0x64 == -46.8f, the D_0081069C > 8.6f
 * straight-out gate and the 0.3-deg-per-unit swing all match).
 * Consumes the
 * commit's horiz_dist (D_00810690, one frame stale — engine) and the
 * ACTUAL pair's horizontal distance (D_0081069C). */
static void camera_walk_eye_0022FCA0(EmCamera *cam)
{
    float follow = fabsf(g.cam_dist_param);          /* fabs(cam+0x0C) */
    float slack  = (g.cam_dist_param == -46.8f) ? CAM_TETHER_SLACK
                                                : 10.0f; /* cam+0x64 rule */
    float excess = cam->horiz_dist - follow;

    if (excess > 0.0f) {
        /* too far: drag the eye toward the target by the FULL excess
         * (the tow-rope — the eye follows the player's path). */
        float d[3] = { cam->tgt_des[0] - cam->eye_des[0], 0.0f,
                       cam->tgt_des[2] - cam->eye_des[2] };
        cam_norm3(d);
        cam->eye_des[0] += d[0] * excess;
        cam->eye_des[2] += d[2] * excess;
        cam->swing = 0;                          /* cam+0x03 = 0 */
        return;
    }
    if (excess >= -slack) {
        cam->swing = 0;          /* dead band: nothing moves (engine
                                  * .L0022FFE4) */
        return;
    }
    /* over-closed past the slack */
    {
        float over = excess + slack;             /* < 0 (spad 3A24) */
        float ax = cam->eye[0] - cam->tgt[0];    /* D_0081069C: the */
        float az = cam->eye[2] - cam->tgt[2];    /* ACTUAL pair     */
        if (ax * ax + az * az > CAM_TETHER_NEAR * CAM_TETHER_NEAR) {
            /* back straight out to follow - slack (the engine leaves
             * the swing latch untouched here — verbatim). */
            float d[3] = { cam->tgt_des[0] - cam->eye_des[0], 0.0f,
                           cam->tgt_des[2] - cam->eye_des[2] };
            cam_norm3(d);
            cam->eye_des[0] += d[0] * over;      /* over < 0 -> away */
            cam->eye_des[2] += d[2] * over;
            return;
        }
        /* cramped (actual eye within 8.6 u of the target): SWING.
         * Latch the direction once, against the last blocking wall's
         * published heading (cam+0x90 — zeroed at init, written by
         * the follow solver); the bearing accumulates in swing_yaw
         * (spad 0x70003A28) across latched frames. */
        if (cam->swing == 0) {
            cam->swing_yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                                    cam->tgt_des[2] - cam->eye_des[2]);
            cam->swing = (cam->wall_yaw - cam->swing_yaw > 0.0f) ? 1 : 2;
        }
        {
            float step = EM_PI * (CAM_SWING_DPU * over) / 180.0f;
            cam->swing_yaw += (cam->swing == 1) ? step : -step;
            cam->eye_des[0] += sinf(cam->swing_yaw) * over;
            cam->eye_des[2] += cosf(cam->swing_yaw) * over;
        }
    }
}

/* Shortest-arc yaw seek at `rate` rad/s. Returns 1 once aligned. */
static int cam_yaw_seek(EmCamera *cam, float target, float rate)
{
    float diff = target - cam->yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    float step = rate * FRAME_DT;
    if (fabsf(diff) <= step) {
        cam->yaw = target;
        return 1;
    }
    cam->yaw += diff > 0.0f ? step : -step;
    if (cam->yaw >  EM_PI) cam->yaw -= 2.0f * EM_PI;
    if (cam->yaw < -EM_PI) cam->yaw += 2.0f * EM_PI;
    return 0;
}

/* func_00194D10 — the director's fixed-camera trigger-volume test:
 * player XZ inside the region quad (func_001B1EA0 mode 0; all shipped
 * records are axis-aligned rects) AND |player.y - record.y| < 4.0.
 * Returns the active region or NULL. */
static const EmCamRegion *camregion_find(void)
{
    for (int i = 0; i < g.n_camregion; i++) {
        const EmCamRegion *cr = &g.camregion[i];
        if (g.pos[0] >= cr->x0 && g.pos[0] <= cr->x1 &&
            g.pos[2] >= cr->z0 && g.pos[2] <= cr->z1 &&
            fabsf(g.pos[1] - cr->ygate) < CAM_REGION_YGATE)
            return cr;
    }
    return NULL;
}

/* Is the default-height eye position at `yaw` wall-blocked from the
 * current look target? (the idle auto-orient's rotation-path test) */
static int cam_yaw_blocked(const EmCamera *cam, float yaw)
{
    if (!g.coll.poly_count)
        return 0;
    float eye[3] = { g.pos[0] - sinf(yaw) * CAM_DIST,
                     g.pos[1] + CAM_EYE_HEIGHT,
                     g.pos[2] - cosf(yaw) * CAM_DIST };
    EmCollHit hit;
    return em_collision_segment_query(&g.coll, (float *)cam->tgt_des, eye,
                                      EM_COLL_SET_CELLS | EM_COLL_SET_GRID,
                                      EM_COLL_ID_NONE, &hit) != 0;
}

/* func_0018BC20 — mode dispatch (struct byte +0x06 over the cut/smooth
 * jump tables jtbl_0026D950/jtbl_0026D910). Natively only MODE 0 exists:
 * the generic player-relative follow (the smooth-table inline follow).
 * On the PS2, cut-table mode 0 is func_00195130 — the per-AREA camera
 * DIRECTOR, whose per-room logic lives in the area overlays (hardcoded
 * `jal 0x823FE0` hook): the survival-horror PER-ROOM FIXED/rail camera
 * angles. Those fixed angles are real and pending: translating the
 * overlay directors lands them HERE as cut-table mode 0.
 * TODO(camera-modes): translate the overlay directors and handlers 1..15
 * as the overlay code is decompiled — one-shot reposition (5 -> 7),
 * timed hold (6), init/fallback settle (8, func_001914A0), 9..15, and
 * the scope/sniper camera (top-mode 3, func_0022EEF0, zoom 224/x). */
/* MODE 1 — the over-shoulder AIM camera, DECODED (func_00197D20
 * dispatcher + func_00197740 entry / func_00197870 steady — the "AIM
 * CAMERA MODE 1" constants block above; replaces the old +0x8C
 * target-height stand-in AND the R1 cam_yaw_seek). The entry phase
 * frames the player from the current camera heading; the steady phase
 * looks from 30 u behind the FACING toward a point 16 u along the AIM
 * DIRECTION — the view down the barrel toward the laser dot. */
static void camera_mode1_aim(EmCamera *cam)
{
    /* sub 1 -> 2 when the aim pose commits (engine: player +0x1F1 == 1
     * gates the sub advance in func_00197D20) */
    if (cam->aim_phase == 0) cam->aim_phase = 1;
    if (cam->aim_phase == 1 && em_game_anim_active() == 0x112)
        cam->aim_phase = 2;

    if (cam->aim_phase == 1) {
        /* func_00197740: TARGET = player + rotY(cam euler)*(0,19,6),
         * EYE = player + rotY*(0,19,-30). cam+0x30's yaw here is the
         * chase camera's committed heading. */
        float ey = cam->yaw;
        cam->tgt_des[0] = g.pos[0] + sinf(ey) * CAM_AIM_ENTRY_FWD;
        cam->tgt_des[1] = g.pos[1] + CAM_AIM_TGT_UP;
        cam->tgt_des[2] = g.pos[2] + cosf(ey) * CAM_AIM_ENTRY_FWD;
        cam->eye_des[0] = g.pos[0] - sinf(ey) * CAM_AIM_EYE_BACK;
        cam->eye_des[1] = g.pos[1] + CAM_AIM_TGT_UP;
        cam->eye_des[2] = g.pos[2] - cosf(ey) * CAM_AIM_EYE_BACK;
        return;
    }

    /* func_00197870 steady. The R2 stance (player state 0x2A) bases
     * the target on the position SAVED AT ENTRY (spad D_70003040);
     * the R1 stance (0xD) tracks the live position. */
    float dir[3];
    aim_dir_get(dir);
    const float *base = (g.r2_aim && !em_weapon_is_aiming())
                      ? cam->aim_entry : g.pos;
    cam->tgt_des[0] = base[0] + dir[0] * CAM_AIM_TGT_FWD;
    cam->tgt_des[1] = base[1] + CAM_AIM_TGT_UP + dir[1] * CAM_AIM_TGT_FWD;
    cam->tgt_des[2] = base[2] + dir[2] * CAM_AIM_TGT_FWD;

    /* EYE: 30 u behind the player FACING (rotEuler(player rot) — it
     * tracks the turn-in-place), height countering the aim pitch. */
    float v = -CAM_AIM_EYE_BACK * dir[1];     /* -30*dir.y */
    float f20 = 0.0f;
    if (v < -22.0f) {
        if (v < -25.0f) v = -25.0f;
        f20 = 22.0f + v;                      /* in [-3, 0] */
    }
    cam->eye_des[0] = g.pos[0] - sinf(g.yaw) * CAM_AIM_EYE_BACK;
    cam->eye_des[2] = g.pos[2] - cosf(g.yaw) * CAM_AIM_EYE_BACK;
    float eyy = g.pos[1] + CAM_AIM_TGT_UP + v;
    if (eyy > g.pos[1] + 30.0f) eyy = g.pos[1] + 30.0f;
    if (eyy < g.pos[1] + 2.0f)  eyy = g.pos[1] + 2.0f;  /* flag areas: +11 */
    /* anti-close (the < 7 u raise arm) */
    {
        float dx = cam->eye_des[0] - g.pos[0];
        float dz = cam->eye_des[2] - g.pos[2];
        if (sqrtf(dx * dx + dz * dz) < 7.0f) {
            float floor_y = g.pos[1] + 18.0f + f20;
            if (eyy < floor_y) eyy = floor_y;
        }
    }
    cam->eye_des[1] = eyy;
    /* struct yaw (+0x44/+0x9C bookkeeping): the sight-line heading */
    cam->yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                      cam->tgt_des[2] - cam->eye_des[2]);
}

static void camera_mode_dispatch(EmCamera *cam)
{
    /* The original gives the player NO free camera control — the only
     * yaw inputs are the R1/R2 aim camera (mode 1), the L1 orient-
     * behind and the idle auto-orient. All of them steer the authentic
     * struct yaw (+0x44)/desired vectors; everything downstream
     * consumes only those, exactly like an engine mode handler. */
    const EmFrameInput *in = em_frame_input();
    int aim_on = em_weapon_is_aiming() || g.r2_aim;

    /* FIXED-CAMERA REGION (the mode-0 director's decoded room cameras —
     * scene.txt `camregion`): inside, the room OWNS the camera. L1 is a
     * NO-OP ("L1 won't reorient because the room has a specified camera
     * angle"), the idle auto-orient is off, and only the aim camera
     * takes control — releasing it falls back to the room spec the
     * SAME frame (the instant snap, applied below). */
    const EmCamRegion *rg = camregion_find();
    g.cam_region_on = (rg != NULL) && !aim_on;

    /* MODE 1 — aim camera (decoded above; it runs inside fixed-camera
     * regions too: "the R1 aim camera still runs"). */
    if (aim_on) {
        camera_mode1_aim(cam);
        g.cam_recenter = 0;
        cam->timer    = 0;
        cam->orbit_on = 0;
        return;
    }

    if (rg) {
        g.cam_recenter = 0;          /* L1/R1-tap reorients are ignored */
        cam->timer     = 0;          /* idle auto-orient disabled */
        cam->orbit_on  = 0;
    } else {

    /* L1: one-shot orient behind the player. The ARM is DECODED
     * (func_00191000, s64): the mode-0 router func_00193EB0 (player
     * states 1/0x21, cam mode 0, not fixed) and the walk camera
     * func_00230000 check the press edge of the config halfword at
     * spad 0x70003B80 (the L1 slot of the decoded config block; the
     * port's EM_PAD_L1 maps the same default binding), goal yaw = the
     * player heading +0xC4 (+pi when action code +0x1F0 == 6 — no
     * native producer, untranslated), a 3-deg deadband DROPS already-
     * aligned presses, and the orbit radius cam+0x4C = the actual
     * horiz eye<->target distance |D_0081069C| clamped into [7.0,
     * |cam+0x64| = 46.8]. (All four re-verified against
     * src/func_001B0080.c's sibling src/func_00191000.c: the gate is
     * `D_00810E74 & *(u16*)0x70003B80`, the deadband literal is
     * 0.05235988f = 3 deg, and the clamp order is `< 7 -> 7` else
     * `> fabs(cam+0x64) -> fabs(cam+0x64)`.)  It writes cam+0x06 = 3
     * and cam+0x01 = 0 — PROVENANCE NOTE: +0x06 is the MODE byte this
     * file's own func_0018BC20 comment describes, so calling this
     * "sub-state 3" below is loose; the state byte +0x01 is what
     * func_00193D90/func_001921D0 drive. Its MOTION
     * handler is still unread: the port seeks at the orient-family
     * rate (CAM_L1_RATE, flagged) around the armed radius (placement
     * shape inferred from the sub-state-2 orbit, flagged). R1 no
     * longer seeks here — the aim camera above owns the armed
     * stance. */
    if (in->pressed & EM_PAD_L1) {
        float diff = cam_wrap_pi(g.yaw - cam->yaw);
        if (fabsf(diff) > CAM_IDLE_ORIENT_DEADBAND) {  /* 3 deg */
            float rx = cam->eye[0] - cam->tgt[0];
            float rz = cam->eye[2] - cam->tgt[2];
            float rad = sqrtf(rx * rx + rz * rz);
            if (rad < CAM_L1_MIN_RAD)   rad = CAM_L1_MIN_RAD;
            if (rad > SOLV_DIST_PARAM)  rad = SOLV_DIST_PARAM;
            cam->orbit_rad = rad;                      /* +0x4C */
            g.cam_recenter = 1;                        /* sub-state 3 */
        }
    }
    if (g.cam_recenter) {
        if (cam_yaw_seek(cam, g.yaw, CAM_L1_RATE * 60.0f))
            g.cam_recenter = 0;
        cam->timer    = 0;
        cam->orbit_on = 0;
    }

    /* IDLE AUTO-ORIENT — DECODED (the constants block above): the
     * struct timer +0x08 runs while the player is idle/walking with a
     * clear solver byte; at 481 frames (= the fidget timer 300 + the
     * 180-frame look-around clip: the END of the look-around idle) it
     * arms the 0.2 deg/frame ORBIT (state byte +0x01 = 2, motion
     * handler func_00193D90) around the saved eye<->target radius,
     * stopping at walls and on any player action.
     * RE-VERIFIED against src/func_001921D0.c, which is the function
     * that actually owns this timer:
     *   if (cam[7] & 9) cam+8 = 0;
     *   else if (player+0x230 == 1 || == 2) { if (++cam+8 >= 0x1E1) ... }
     *   else cam+8 = 0;
     * -> mask 9, the 481 (0x1E1) threshold, the 0.05235988f deadband and
     * the "idle action codes 1/2 only" gate are all engine-literal. The
     * arm writes cam+0x48 = player+0xC4, cam+0x4C = fabs(D_0081069C),
     * cam+0x01 = 2 and the arc side at cam+0x03, and is vetoed by
     * cam[7] & 4 (turning negative) / & 2 (turning positive) — the port
     * substitutes a trial-yaw wall probe for those two solver bits.
     * The 0.2 deg/frame step itself is the 0x3B64C389 literal that
     * src/func_00193D90.c feeds to func_001B12B0. */
    int idle_ok = g.gait == 0 && g.move_speed <= 0.0f &&
                  !g.cam_recenter && !(in->held & EM_PAD_L1) &&
                  !em_weapon_is_melee();
    if (!idle_ok) {
        cam->timer    = 0;
        cam->orbit_on = 0;
    } else if (cam->orbit_on) {
        float diff = cam->orbit_tgt - cam->yaw;
        while (diff >  EM_PI) diff -= 2.0f * EM_PI;
        while (diff < -EM_PI) diff += 2.0f * EM_PI;
        float step  = CAM_ORBIT_RATE;       /* rad/FRAME (0x3B64C389) */
        float trial = cam->yaw + (diff > 0.0f ? step : -step);
        int   blocked;
        if (fabsf(diff) <= step) {
            cam->yaw      = cam->orbit_tgt; /* aligned: back to follow */
            cam->orbit_on = 0;
            blocked       = 0;
        } else if (!(blocked = cam_yaw_blocked(cam, trial))) {
            cam->yaw = trial;
        } else {
            cam->orbit_on = 0;              /* wall in the path: cancel
                                             * (engine: solver bits
                                             * 0xD/0xB by direction) */
        }
        static int trace = -1;              /* EM_CAMERA_TRACE debug */
        if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
        if (trace && (g.frame_no % 30 == 0 || blocked))
            printf("camera: frame %d auto-orient yaw %.3f -> player "
                   "%.3f%s\n", g.frame_no, cam->yaw, g.yaw,
                   blocked ? " [WALL — stopped]" : "");
    } else {
        if (cam->hit & 9)                   /* engine mask 9 — the solver
                                             * bits are REAL now (bit 8 =
                                             * ceiling involvement, bit 1
                                             * = side other-class; a
                                             * plain wall pull-in is 0
                                             * and does NOT reset, s46/
                                             * s61 decode) */
            cam->timer = 0;
        else if (++cam->timer >= CAM_IDLE_ORIENT_FRAMES) {
            cam->timer = 0;
            float diff = g.yaw - cam->yaw;
            while (diff >  EM_PI) diff -= 2.0f * EM_PI;
            while (diff < -EM_PI) diff += 2.0f * EM_PI;
            if (fabsf(diff) > CAM_IDLE_ORIENT_DEADBAND &&
                !cam_yaw_blocked(cam, cam->yaw + (diff > 0.0f
                                                  ? CAM_ORBIT_RATE
                                                  : -CAM_ORBIT_RATE))) {
                cam->orbit_on  = 1;          /* cam+0x01 = 2 */
                cam->orbit_tgt = g.yaw;      /* cam+0x48 */
                float rx = cam->eye[0] - cam->tgt[0];
                float rz = cam->eye[2] - cam->tgt[2];
                cam->orbit_rad = sqrtf(rx * rx + rz * rz); /* +0x4C */
            }
        }
    }

    }   /* !rg — free-camera orient inputs */

    /* OPENING-CAMERA SEAT (scene.txt `opencam`, s79 it4): while the
     * declared scene is in its untouched idle opening, pin the emergent
     * pose the byte-faithful solver cannot reach — target = the pinned
     * look-at, eye x/z = the settled +X position, eye.y SEEKS up from the
     * seat height ey0 to the settled ey1 (the live +9.55 wall-clear rise)
     * at the engine eye-Y rate. DISARMS PERMANENTLY on any action (the
     * player moves, R1 aims, L1/recenter, the 481-frame auto-orbit, or a
     * fixed region) — exactly the engine's "cancels on any action"; from
     * then on the normal solver owns the camera. camera_solve still runs
     * below: at the pinned +X eye its primary probe is clear, so it is a
     * no-op horizontally (PCSX2: the dead-band-frozen settle). */
    if (g.opencam_on) {
        int disarm = g.move_speed != 0.0f || cam->aim_phase ||
                     cam->orbit_on || g.cam_recenter || g.cam_region_on;
        if (disarm) {
            g.opencam_on = 0;        /* hand the camera back to the solver */
        } else {
            cam->tgt_des[0] = g.opencam_tgt[0];
            cam->tgt_des[1] = g.opencam_tgt[1];
            cam->tgt_des[2] = g.opencam_tgt[2];
            cam->eye_des[0] = g.opencam_eye[0];
            cam->eye_des[2] = g.opencam_eye[2];
            /* eye.y rise: seek the settled height at the engine eye-Y
             * step (func_00191D40: |d|/10 capped 4.0, d/5 inside 1 u). */
            cam_eye_y_seek_00191D40(cam, g.opencam_eye[1], CAM_EYE_CAP);
            cam->yaw = atan2f(cam->tgt_des[0] - cam->eye_des[0],
                              cam->tgt_des[2] - cam->eye_des[2]);
            cam->swing = 0;
            if (g.opencam_idle < 100000) g.opencam_idle++;
            return;
        }
    }

    /* Mode 0 generic follow — the target side is func_001916C0
     * (decoded s65, cut-table cases): desired target chases the player
     * x/z at <= 2.0 u/frame and its height seeks player.y + 11 +
     * cam[0x8C] at <= 4.0 — i.e. +17 idle, +8 while moving (the walk
     * table writes 0x8C = -3). The first case adds the 0.3 *
     * shaped(excess) DIP term to the height want.
     *
     * AUDIT CORRECTION: the DIP case is NOT "idle only". src/func_001916C0.c
     * puts action codes {0, 1, 3, 14, 20, 21, 22} in the dip arm and only
     * {2, 4, 15} in the no-dip arm — and src/func_00191390.c gives that
     * same {2, 4, 0xF} set the -3.0/1.0 height row, i.e. it is the +0x236
     * ELEVATED/hang family, exactly as this file's own func_00191390 note
     * (above camera_prestep_00191390) already says. A ground WALK is action
     * code 3 (src/func_0015CBA0.c maps player states 1/6/7/15/58 -> 3 with
     * no +0x236), which is IN the dip arm. The port models no +0x236 latch,
     * so every state it can reach (codes 1 and 3) takes the dip; the old
     * `g.move_speed == 0` gate made the target height jump the moment the
     * player started walking while the camera was over-close. */
    cam->tgt_des[0] = cam_chase_h(cam->tgt_des[0], g.pos[0], CAM_TGT_CAP_XZ);
    cam->tgt_des[2] = cam_chase_h(cam->tgt_des[2], g.pos[2], CAM_TGT_CAP_XZ);
    {
        float want = g.pos[1] + CAM_BASE_H + cam->aim_h;
        {
            /* DIP: excess = desired horiz eye<->tgt dist (cam+0x0C
             * param's fabs) — the func_001916C0 spad D_70003A20. Uses
             * the commit's horiz_dist (1 frame stale, engine-true). */
            float follow = fabsf(g.cam_dist_param);
            float slack  = (g.cam_dist_param == -46.8f) ? CAM_TETHER_SLACK
                                                        : 10.0f;
            float excess = cam->horiz_dist - follow;
            if (excess < -slack) {                 /* over-close re-map */
                float f1 = -2.0f * slack - excess;
                excess = (f1 <= -CAM_TGT_DIP_CAP) ? f1 : -CAM_TGT_DIP_CAP;
            }
            want += CAM_TGT_DIP_K * excess;
        }
        cam->tgt_des[1] = cam_chase_v(cam->tgt_des[1], want, CAM_TGT_CAP_Y);
    }

    /* AUTO-ORIENT ORBIT eye (func_00193D90): the desired eye circles
     * the target at the radius saved when the orbit armed. The L1
     * seek (sub-state 3) rides the same +0x4C radius — its decoded
     * arm (func_00191000) wrote it above; the placement shape is the
     * sub-state-2 one (motion handler unread, flagged). */
    if ((cam->orbit_on || g.cam_recenter) && !g.cam_region_on) {
        cam->eye_des[0] = cam->tgt_des[0] - sinf(cam->yaw) * cam->orbit_rad;
        cam->eye_des[1] = g.pos[1] + CAM_EYE_HEIGHT;
        cam->eye_des[2] = cam->tgt_des[2] - cosf(cam->yaw) * cam->orbit_rad;
        cam->swing = 0;
    } else if (g.cam_region_on) {
        /* ROOM SPEC: pin the desired eye to the region's fixed eye (the
         * director writes cam+0x10/14/18 = the record spec; the target
         * above keeps tracking the player, like the snow case). The
         * struct yaw becomes the fixed sight-line heading so the
         * camera-relative movement controls stay coherent, and an R1
         * release re-enters here the SAME frame — camera_solve's region
         * path hard-copies eye = spec (no chase): the observed INSTANT
         * snap-back. */
        cam->eye_des[0] = rg->eye[0];
        cam->eye_des[1] = rg->eye[1];
        cam->eye_des[2] = rg->eye[2];
        cam->yaw = atan2f(g.pos[0] - rg->eye[0], g.pos[2] - rg->eye[2]);
        cam->swing = 0;
    } else {
        /* THE EMERGENT FOLLOW CAMERA — both the MOVING-PLAYER tail
         * (func_00230000) and the IDLE tail (func_001921D0
         * .L00192DDC) run the SAME machinery, so the port collapses
         * them into one branch (s76 idle-emergence pass — retires
         * PORT_DIFFERENCES D14, the old yaw-anchored idle seat).
         *
         *  - The desired eye x/z rides the tow-rope TETHER
         *    (func_0022FCA0: drag at |camdist| = 46.8, the
         *    [follow-slack, follow] dead-band freeze) over the
         *    previous frame's actual eye.
         *  - The desired eye height seeks the IDLE row +19
         *    (func_00191D40; the port has no +0x236 elevated latch so
         *    the row is constant idle — no walk dive, PCSX2-verified).
         *  - The wall solver (cam_solver_0018DD20, run in
         *    camera_solve below) then pulls the seat in / shoves it
         *    laterally / raises it over the AREA-11 entry wall.
         *  - The struct yaw is a DERIVED OUTPUT = atan2(target - eye),
         *    recomputed each frame from the ACTUAL pair (one frame
         *    stale, engine end-of-frame write), NEVER an input.
         *
         * IDLE specifically: the entry seat (camera_entry_seat, 46.8
         * behind the spawn yaw, lands in a wall) gives the solver the
         * right starting point to pull in from, and the dead-band
         * then FREEZES the settled result — the idle camera is
         * emergent, not a fixed pose. The 481-frame idle auto-orbit
         * stays gated above (the orbit branch handles cam->orbit_on);
         * this is the pre-orbit idle the AREA-11 opening shows. */
        camera_walk_eye_0022FCA0(cam);
        cam_eye_y_seek_00191D40(cam,
                                g.pos[1] + CAM_BASE_H + cam->var_5c
                                         + cam->aim_h,
                                CAM_EYE_CAP);
        cam->yaw = atan2f(cam->tgt[0] - cam->eye[0],
                          cam->tgt[2] - cam->eye[2]);
    }

    /* EM_CAMERA_TRACE=1 — region enter/leave transitions (debug). */
    {
        static int trace = -1, was_on = 0;
        if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
        if (trace && g.cam_region_on != was_on)
            printf("camera: frame %d %s camregion%s\n", g.frame_no,
                   g.cam_region_on ? "ENTER" : "LEAVE",
                   g.cam_region_on ? " — eye pinned to the room spec"
                                   : "");
        was_on = g.cam_region_on;
    }
}

float cam_dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void cam_norm3(float v[3])               /* func_00102760 */
{
    float l = sqrtf(cam_dot3(v, v));
    if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

float cam_wrap_pi(float a)               /* func_001B1470 */
{
    while (a >  EM_PI) a -= 2.0f * EM_PI;
    while (a < -EM_PI) a += 2.0f * EM_PI;
    return a;
}

/* func_0018DD20 — THE blocked-eye solver, DECODED 2026-06-11 (s61)
 * from the full 6984-byte .s read; replaces the port-invented rise
 * model (PORT_DIFFERENCES D3). This is the STYLE-0 path — the one the
 * whole generic gameplay camera uses: the idle states (1/0x26/0x27,
 * func_001921D0 tail) and the locomotion states (2/4/0xF,
 * func_00230000) both call func_0018D7B0(cam, 0), which runs this
 * solver over collision mask 6 (static cells + grid, NO movable
 * hulls) and then smooth-chases the actual eye at cap 4.0/frame.
 * (Styles 3/4 — cinematic player states — take a reduced branch in
 * the same function; style 5 = director cams -> func_0018D910 [bounds
 * maintenance only, cam_solver_0018D910 below]; style 2/6 = aim/scope
 * -> func_0018F870 [decoded s64, cam_solver_0018F870 below]. The
 * dispatcher also runs a
 * pre-pass func_0018D330 that publishes sight-ray bits to cam+0x5A
 * and the overhead-ceiling Y to cam+0x60 — no decoded consumer yet,
 * untranslated.)
 *
 * Decoded policy, in order:
 *  1. PRIMARY PROBE: desired target -> desired eye EXTENDED 1.5 u.
 *     No hit -> no first-stage response. On a hit: publish the
 *     surface class (cam+0x58) and the wall heading (cam+0x90), and
 *     classify GLANCING = dot(horiz sight dir, horiz hit normal) <
 *     sin45 (the normal's RAW horizontal component — steep faces
 *     read as glancing, engine-verbatim).
 *  2. HEAD-CLEAR WAIVER (style != 3, dist <= |cam+0x0C| = 46.8):
 *     re-probe from (target.x, player.y + 17.5, target.z) to the
 *     extended eye. CLEAR -> the block is waived outright (low walls
 *     never move the camera — the raised line sees the eye over
 *     them); a ceiling-class hit within 1 u of the eye is forgiven
 *     too.
 *  3. FIRST-STAGE RESPONSE while still blocked:
 *      - ceiling class (0x8800): eye = hit, y -= 1 (duck under),
 *        x/z += 0.5 * sight dir; widen the carried Y bounds to admit
 *        it.   [result bit 8]
 *      - wall (0x2000): REVERSE probe eye -> target; a wall hit
 *        whose normal does NOT oppose the first (dot > -0.3) means
 *        the eye sits in a wedge/behind a corner: eject x/z to 4 u
 *        along the reverse-hit normal, height kept.
 *      - otherwise PULL-IN: eye x/z = hit + 0.5 * sight dir, HEIGHT
 *        KEPT. (The "apparent rise": constant absolute height +
 *        collapsing horizontal distance = the camera looks down on
 *        the player's head. No bits set — a plain pull-in returns 0.)
 *  4. SIDE STAGE (sight line clear OR glancing): probe 5.5 u to each
 *     side of the eye perpendicular to the sight line (glancing
 *     variant sweeps from 3 u on the far side to 1.5 u target-ward +
 *     5.5 on the near side, with same-wall/cross-normal rejects and a
 *     confirm probe from the first hit point at eye height). A valid
 *     side hit proposes a candidate at (hit -/+ side vector) + 0.1
 *     along the probe ray — i.e. the eye slides to keep ~5.4 u of
 *     lateral clearance [bits 2 (left) / 4 (right), 8 = ceiling-
 *     underside candidate keeps its own Y, 1 = other-class]. BOTH
 *     sides hitting centers the eye x/z on the midpoint of the two
 *     hit points (tight corridors). Engine quirk kept verbatim: the
 *     RIGHT side's ceiling-underside and other-class candidates set
 *     bits 8/1 but not 4, so the selection below never applies them.
 *  5. FINAL Y POLICY: if blocked by a NON-wall class: ceiling ->
 *     eye.y = min(eye.y, hit.y - 1); floor class -> eye.y =
 *     max(eye.y, hit.y + 1) — a LITERAL 1-u rise over floor-class
 *     blockers (banks/ledges clipping the sight line). Then clamp
 *     eye.y into the carried bounds, RE-PROBE the bounds (from the
 *     eye pulled 1.5 u toward player+11up: floor 200 down [class
 *     0x7000, engine-verbatim includes WALL] + 17 -> lower; ceiling
 *     200 up [0x8800] - 1 -> upper, else eye + 200; lower forced to
 *     upper - 3 if crossed) and clamp again [bits 0x40 floor-clamped
 *     / 0x80 ceiling-clamped]. The eye can NEVER sink below 17 u
 *     over the floor beneath it nor poke above the ceiling.
 *  (Engine per-area specials not reachable by the port's scenes are
 *  omitted, flagged: area 0x12 eye-z clamp [169.5, 230.6] + its
 *  upward bound probe variant, area 0x15 z>260 floor-bound 70.)
 *
 * Returns the result-bit byte (-> cam->hit, struct +0x07). Mutates
 * cam->eye_des in place exactly like the engine mutates cam+0x10 —
 * the mode handler re-poses it every frame, so nothing carries over.
 *
 * The actual eye (D_008105D0) then smooth-chases the solved desired
 * eye per axis, capped 4.0 u/frame (dispatcher style-0 tail) — every
 * response above inherits the engine's own smoothing; the actual
 * target is a straight copy of the desired target (func_0018C0C0). */
static int cam_solver_0018DD20(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID; /* 6 */
    float *eye = cam->eye_des;                /* cam+0x10, in place */
    const float *tgt = cam->tgt_des;          /* cam+0x20 */
    EmCollHit hit;
    int   blocked, glancing = 0, bits = 0;
    int   side_l = 0, side_r = 0;     /* validated side hits (s1/s7) */
    float ext_eye[3];                 /* spad 38A0 (extended eye)    */
    float first_pt[3] = { 0, 0, 0 };  /* spad 38C0/3950 (1st hit)    */
    float first_n[3]  = { 0, 0, 0 };  /* spad 38E0 (1st hit normal)  */
    float hdir[3]     = { 0, 0, 0 };  /* spad 3960 (horiz sight dir) */
    float lpt[3] = { 0, 0, 0 }, rpt[3] = { 0, 0, 0 }; /* 3920/3930  */
    float cand_a[3], cand_b[3];       /* sp+0xA0 / sp+0xB0           */
    uint16_t attr = 0;

    /* 1. PRIMARY PROBE — target -> eye extended 1.5 u past the eye. */
    {
        float d[3] = { eye[0] - tgt[0], eye[1] - tgt[1], eye[2] - tgt[2] };
        cam_norm3(d);
        ext_eye[0] = eye[0] + SOLV_EXT * d[0];
        ext_eye[1] = eye[1] + SOLV_EXT * d[1];
        ext_eye[2] = eye[2] + SOLV_EXT * d[2];
    }
    blocked = em_collision_segment_query(&g.coll, tgt, ext_eye, mask,
                                         EM_COLL_ID_NONE, &hit) != 0;
    if (blocked) {
        attr = hit.surf_class;
        cam->hit_attr = attr;                          /* cam+0x58 */
        /* AUDIT CORRECTION (src/func_0018DD20.c): the engine sets its
         * result byte to 1 the moment the primary probe hits — BEFORE
         * the head-clear waiver ("touched = 1;" immediately inside
         * `if (probeHit != 0)`), and the waiver only clears probeHit,
         * never touched. The port previously left the bits at 0 for a
         * plain pull-in, so `cam->hit & 9` never fired and the idle
         * auto-orbit armed while the sight line was blocked. */
        bits = 1;
        memcpy(first_pt, hit.point, sizeof first_pt);
        memcpy(first_n, hit.normal, sizeof first_n);
        hdir[0] = tgt[0] - ext_eye[0];
        hdir[1] = 0.0f;                                /* horizontal */
        hdir[2] = tgt[2] - ext_eye[2];
        cam_norm3(hdir);
        /* glancing gate: RAW horizontal normal component (verbatim) */
        if (hdir[0] * first_n[0] + hdir[2] * first_n[2] < SOLV_GLANCE_COS)
            glancing = 1;
        cam->wall_yaw = cam_wrap_pi(atan2f(first_n[0], first_n[2]));

        /* 2. HEAD-CLEAR WAIVER (style != 3; dist inside the param). */
        if (cam->horiz_dist - SOLV_DIST_PARAM <= 0.0f) {
            float h = (cam->var_5c == 1.0f) ? SOLV_HEADCLR_H1
                                            : SOLV_HEADCLR_H;
            float start[3] = { tgt[0], g.pos[1] + h, tgt[2] };
            EmCollHit h2;
            if (!em_collision_segment_query(&g.coll, start, ext_eye, mask,
                                            EM_COLL_ID_NONE, &h2)) {
                blocked = 0;            /* raised line sees the eye */
            } else if (h2.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                float dx = h2.point[0] - ext_eye[0];
                float dy = h2.point[1] - ext_eye[1];
                float dz = h2.point[2] - ext_eye[2];
                if (dx * dx + dy * dy + dz * dz < 1.0f)
                    blocked = 0;        /* ceiling graze AT the eye */
            }
        }

        /* 3. FIRST-STAGE RESPONSE. */
        if (blocked) {
            int handled = 0;
            float dir[3] = { ext_eye[0] - tgt[0], ext_eye[1] - tgt[1],
                             ext_eye[2] - tgt[2] };
            cam_norm3(dir);
            if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {  /* 0x8800 */
                eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
                eye[1] = first_pt[1] - 1.0f;     /* duck under it */
                eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
                if (eye[1] < cam->y_lo) cam->y_lo = eye[1];
                if (eye[1] > cam->y_hi) cam->y_hi = eye[1];
                bits = 8;         /* ASSIGN — engine "touched = 8;"
                                   * clears the primary-hit bit 1 */
                handled = 1;
            } else if (attr & EM_SURF_WALL) {               /* 0x2000 */
                EmCollHit rh;
                if (em_collision_segment_query(&g.coll, ext_eye, tgt,
                                               mask, EM_COLL_ID_NONE,
                                               &rh) &&
                    (rh.surf_class & EM_SURF_WALL) &&
                    cam_dot3(rh.normal, first_n) > SOLV_WEDGE_DOT) {
                    /* wedge: the eye is buried behind a corner */
                    cam->wall_yaw = cam_wrap_pi(atan2f(rh.normal[0],
                                                       rh.normal[2]));
                    eye[0] = rh.point[0] + SOLV_WEDGE_EJECT * rh.normal[0];
                    eye[2] = rh.point[2] + SOLV_WEDGE_EJECT * rh.normal[2];
                    handled = 1;
                }
            }
            if (!handled) {
                /* PULL-IN, height kept — the "apparent rise". */
                eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
                eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
            }
            /* EM_CAMERA_TRACE=1 — first-stage branch (debug). */
            {
                static int trace = -1;
                if (trace < 0)
                    trace = getenv("EM_CAMERA_TRACE") != NULL;
                if (trace)
                    printf("camera: frame %d 0018DD20 BLOCKED attr "
                           "0x%04x %s%s-> des eye %.2f %.2f %.2f "
                           "(tgt %.2f %.2f %.2f)\n", g.frame_no, attr,
                           handled ? (bits & 8 ? "ceiling-duck "
                                               : "wedge-eject ")
                                   : "pull-in ",
                           glancing ? "[glancing] " : "",
                           eye[0], eye[1], eye[2],
                           tgt[0], tgt[1], tgt[2]);
            }
        }
    }

    /* 4. SIDE STAGE — runs when the sight line is CLEAR **or** the block
     * is GLANCING. AUDIT CORRECTION: the recovered C (src/func_0018DD20.c)
     * spells the gate out as
     *   doSlide = 1;
     *   if (steep != 1) { doSlide = 0; if (probeHit == 0) doSlide = 1; }
     * i.e. doSlide = glancing || !blocked  ("steep" is set exactly where
     * this function classifies GLANCING, `if (temp_f0 < 0.707f) steep=1;`).
     * The port had `!blocked || !glancing`, which is the complement on the
     * blocked half: it ran the side stage on SQUARE-ON blocks (where the
     * engine skips it) and skipped it on GLANCING blocks (where the engine
     * runs it). The s79 note claiming the earlier "clear or glancing"
     * reading "was backwards" is itself the error.
     * NOTE (s79 it2): su is built from yaw - pi/2 = PERPENDICULAR to the
     * sight = PARALLEL to a square-on wall, so these 5.5-u probes run
     * ALONG the faced wall and only catch PERPENDICULAR walls (corners /
     * corridor sides). They are a corridor centerer, NOT a 44-u swing —
     * the AREA-11 -X->+X crossover is NOT produced here (it is a
     * collision/entry-placement blocker; see FINDINGS s79 it2). */
    if (!blocked || glancing) {
        float yaw  = atan2f(tgt[0] - eye[0], tgt[2] - eye[2]);
        float syaw = cam_wrap_pi(yaw - EM_PI * 0.5f);
        float su[3] = { sinf(syaw), 0.0f, cosf(syaw) };  /* unit side */
        float sv[3] = { SOLV_SIDE * su[0], 0.0f, SOLV_SIDE * su[2] };
        float twd[3] = { 0, 0, 0 };       /* unit eye-ward (square-on) */
        float start[3], end[3], gate;
        EmCollHit sh;

        if (blocked) {
            twd[0] = eye[0] - tgt[0];
            twd[1] = eye[1] - tgt[1];
            twd[2] = eye[2] - tgt[2];
            cam_norm3(twd);
        }
        cand_a[0] = cand_b[0] = eye[0];   /* sp+0xA0 / sp+0xB0 init */
        cand_a[1] = cand_b[1] = eye[1];
        cand_a[2] = cand_b[2] = eye[2];

        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;  /* left, then right */
            if (!blocked) {
                start[0] = eye[0];
                start[1] = eye[1];
                start[2] = eye[2];
                end[0] = eye[0] + sgn * sv[0];
                end[1] = eye[1];
                end[2] = eye[2] + sgn * sv[2];
            } else {
                /* square-on sweep: far side -> 1.5 target-ward + near */
                start[0] = eye[0] - sgn * SOLV_SIDE_BACK * su[0];
                start[1] = eye[1];
                start[2] = eye[2] - sgn * SOLV_SIDE_BACK * su[2];
                end[0] = eye[0] - SOLV_SIDE_FWD * twd[0] + sgn * sv[0];
                end[1] = eye[1] - SOLV_SIDE_FWD * twd[1];
                end[2] = eye[2] - SOLV_SIDE_FWD * twd[2] + sgn * sv[2];
            }
            int hit_ok = em_collision_segment_query(&g.coll, start, end,
                                                    mask, EM_COLL_ID_NONE,
                                                    &sh) != 0;
            gate = 0.0f;
            if (hit_ok && blocked) {
                /* validation (square-on case only reaches here; the
                 * engine skips it when clear, leaving gate = 0) */
                if (bits & 8) {           /* 1st stage was the duck */
                    gate = cam_dot3(sh.normal, hdir);
                    if (gate < SOLV_CROSS_DOT) { hit_ok = 0; gate = -1.0f; }
                } else {
                    gate = cam_dot3(sh.normal, first_n);
                    if (gate < SOLV_OPPOSE_DOT) {
                        hit_ok = 0; gate = -1.0f;
                    } else {
                        float d[3] = { sh.point[0] - end[0],
                                       sh.point[1] - end[1],
                                       sh.point[2] - end[2] };
                        if (cam_dot3(d, d) < 1.0f) {
                            hit_ok = 0; gate = -1.0f; /* far-end graze */
                        } else {
                            /* confirm: 1st hit point at eye height */
                            float cs[3] = { first_pt[0], eye[1],
                                            first_pt[2] };
                            hit_ok = em_collision_segment_query(
                                         &g.coll, cs, end, mask,
                                         EM_COLL_ID_NONE, &sh) != 0;
                            if (hit_ok) {
                                gate = cam_dot3(sh.normal, first_n);
                                if (gate < SOLV_OPPOSE_DOT) {
                                    hit_ok = 0; gate = -1.0f;
                                }
                            } else {
                                gate = -1.0f;
                            }
                        }
                    }
                }
            }
            if (side == 0) side_l = hit_ok; else side_r = hit_ok;
            if (!hit_ok)
                continue;
            memcpy(side == 0 ? lpt : rpt, sh.point, 12);
            /* response window: the engine applies the side response
             * UNCONDITIONALLY when the block is GLANCING (steep == 1);
             * the steep != 1 path — inside this stage that is exactly
             * the CLEAR case, where gate stayed 0 — checks the window
             * gate dot in (-0.3, 0.9). Recovered C:
             *   if ((hitA != 0) && ((steep == 1) ||
             *       ((g = spad3A3C, g < 0.9f) && !(g <= -0.3f))))
             * For the clear case gate == 0, so the window always passes;
             * the test is kept because it mirrors the engine branch. */
            if (!glancing &&
                (!(gate < SOLV_GATE_HI) || gate <= SOLV_GATE_LO))
                continue;
            {
                float rd[3] = { end[0] - start[0], end[1] - start[1],
                                end[2] - start[2] };
                float c[3]  = { sh.point[0] - sgn * sv[0],
                                sh.point[1],
                                sh.point[2] - sgn * sv[2] };
                float *cand = side == 0 ? cand_a : cand_b;
                cam_norm3(rd);
                if (sh.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                    if (-sh.normal[1] < SOLV_GATE_HI) {
                        /* tilted overhang: x/z only */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 2 : 4;
                    } else {
                        /* flat ceiling underside: candidate keeps its
                         * own Y (engine quirk: the right side sets
                         * bit 8 only — never applied below) */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[1] = c[1];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 0xA : 0x8;
                    }
                } else if (sh.surf_class & EM_SURF_WALL) {
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 2 : 4;
                } else {
                    /* other class (floor/slope beside the eye): full
                     * candidate (right side: bit 1 only, quirk) */
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[1] = c[1];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 3 : 1;
                }
            }
        }
        /* selection */
        if ((bits & 6) == 6 || (bits & 2 && side_r) ||
            (bits & 4 && side_l)) {
            eye[0] = 0.5f * (lpt[0] + rpt[0]);   /* corridor centering */
            eye[2] = 0.5f * (lpt[2] + rpt[2]);
        } else if (bits & 2) {
            eye[0] = cand_a[0];                  /* full copy, incl. Y */
            eye[1] = cand_a[1];
            eye[2] = cand_a[2];
        } else if (bits & 4) {
            eye[0] = cand_b[0];
            eye[1] = cand_b[1];
            eye[2] = cand_b[2];
        }
    }

    /* 5. FINAL Y POLICY + BOUNDS. */
    if (blocked && !(attr & EM_SURF_WALL)) {
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
            float lim = first_pt[1] - 1.0f;      /* stay under it */
            if (eye[1] > lim) eye[1] = lim;
        } else {
            float lim = first_pt[1] + 1.0f;      /* literal rise over a
                                                    floor-class blocker */
            if (eye[1] < lim) eye[1] = lim;
        }
    }
    if (eye[1] <= cam->y_lo) eye[1] = cam->y_lo; /* carried bounds */
    if (eye[1] >= cam->y_hi) eye[1] = cam->y_hi;
    {
        float pb[3], lo, hi, probe[3];
        float d[3] = { eye[0] - g.pos[0],
                       eye[1] - (g.pos[1] + SOLV_PLAYER_UP),
                       eye[2] - g.pos[2] };
        cam_norm3(d);
        pb[0] = eye[0] - SOLV_BOUND_PULL * d[0];
        pb[1] = eye[1] - SOLV_BOUND_PULL * d[1];
        pb[2] = eye[2] - SOLV_BOUND_PULL * d[2];
        probe[0] = pb[0];
        probe[1] = pb[1] - SOLV_BOUND_RANGE;     /* floor, 200 down */
        probe[2] = pb[2];
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                               EM_SURF_WALL)))   /* 0x7000, verbatim */
            lo = hit.point[1] + (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1
                                                     : SOLV_FLOOR_PAD);
        else
            lo = cam->y_lo - SOLV_BOUND_RANGE;
        probe[1] = pb[1] + SOLV_BOUND_RANGE;     /* ceiling, 200 up */
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
            hi = hit.point[1] - SOLV_CEIL_PAD;
        else
            hi = eye[1] + SOLV_BOUND_RANGE;
        if (lo > hi) lo = hi - 3.0f;
        cam->y_lo = lo;
        cam->y_hi = hi;
    }
    if (eye[1] <= cam->y_lo) { eye[1] = cam->y_lo; bits |= 0x40; }
    if (eye[1] >= cam->y_hi) { eye[1] = cam->y_hi; bits |= 0x80; }
    return bits;
}

/* func_0018F870 — the AIM/scope wall solver, DECODED 2026-06-11 (s64)
 * from the full 0x16A4 .s read; retires the flagged CAM_WALL_MARGIN
 * stand-in. Style 2 = the aim camera (mode 1, mask 7 — movable hulls
 * IN); style 6 = the scope camera (same first stage, NO lateral
 * stages — not reachable natively, noted inline). The user-observed
 * "R1 keeps the pull-in" is engine truth — the differences from the
 * follow solver are:
 *
 *  1. PRIMARY PROBE runs from the PLAYER POSITION (+0xB0) — not the
 *     desired target, which in aim rides the gun ray 16 u ahead — to
 *     the desired eye extended 1.5 u. A hit sets result bit 1 (the
 *     follow solver's plain pull-in returns 0) and publishes the
 *     class (cam+0x58). GLANCING = dot(horiz TARGET-ward sight dir,
 *     horiz normal) < 0.99 — a much tighter square-on gate than the
 *     follow solver's sin45.
 *  2. FIRST STAGE (every hit — NO head-clear waiver, NO wedge eject):
 *      - non-wall class: widen the carried Y bound to admit the hit
 *        (ceiling-class: y_lo down to hit.y, bit 8; floor-class: y_hi
 *        up to hit.y, bit 0x10), then eye = hit (ALL 3 AXES) + 0.5 *
 *        dir on x/z (dir = unit player->ext-eye) — the eye rides the
 *        blocking surface; the -1/+1 standoff is applied by the final
 *        Y policy below.
 *      - wall (or any other class): PULL-IN, eye x/z = hit + 0.5 *
 *        dir, HEIGHT KEPT — then eye.y clamps into the PREVIOUS
 *        frame's bounds (cam+0x50/+0x54)… UNLESS the hit came from
 *        the GRID walker (engine hub return 4): then the bounds are
 *        RE-DERIVED at (hit - 1*dir) via func_0018CE60 and the clamp
 *        uses the fresh pair.
 *  3. LATERAL STAGE:
 *      - blocked SQUARE-ON (not glancing): the CORNER SLIDE — a lane
 *        1 u off the wall through the eye, swept -3 .. +5.5 u along
 *        the wall face (along = wall-normal yaw - 90). A DIFFERENT
 *        wall in the lane (normal dot vs the first < 0.9) places the
 *        eye 5.5 u back from it along the wall [bits 2 / 4 by side;
 *        the second side only probes if the first found nothing] —
 *        the over-shoulder eye slides out of corner notches.
 *      - clear OR glancing: the 5.5-u SIDE stage, the follow solver's
 *        shape with the same constants (glancing sweep starts 3 u on
 *        the far side; rejects: ceiling-case cross dot < -0.08
 *        against the horiz PLAYER-ward dir, same-wall dot < -0.998;
 *        NO far-end-graze check and NO confirm re-probe — simpler
 *        than the follow solver), candidates at (hit -/+ side vec) +
 *        0.1 along the probe ray, both-sides -> midpoint corridor
 *        centering, right-side bit-8/bit-1 quirks kept verbatim.
 *  4. FINAL Y POLICY (blocked, non-wall class): ceiling-class ->
 *     eye.y = min(eye.y, hit.y - 1); floor-class -> eye.y =
 *     max(eye.y, hit.y + 1).
 *  5. CONFIRM RE-PROBE (any response bits 0x1F): player pos -> eye;
 *     still blocked -> eye x/z = the NEW hit point (no pad).
 *  6. NEW BOUNDS for the NEXT frame (written, NOT applied): from the
 *     eye pulled 1.0 toward player + 11 up, 200 down (class 0x7000)
 *     -> lower = floor + 2.0 (the follow solver's +17 becomes +2: the
 *     aim eye may ride 2 u over the floor — why aiming can look from
 *     ankle height); 200 up (0x8800) -> upper = ceiling - 1, else
 *     eye + 200; lower forced to upper - 3.
 *  (Engine per-area specials omitted, flagged: area 0x12 clamps eye.z
 *  to [169.5, 230.6] and swaps the no-ceiling upper bound for a
 *  player-up probe — with a stale-scratch quirk when THAT misses;
 *  no native area ids.)
 *
 * Returns the result-bit byte -> cam->hit. The mode-1 handler's own
 * eye-Y clamps ([player.y+2, +30]) and the dispatcher's 8-u min-
 * distance push (camera_solve tail) then run downstream, exactly like
 * the engine. */
static int cam_solver_0018F870(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID |
                          EM_COLL_SET_HULLS;          /* style-2 mask 7 */
    float *eye = cam->eye_des;                /* cam+0x10, in place */
    const float *tgt = cam->tgt_des;          /* cam+0x20 */
    EmCollHit hit;
    int   bits = 0, glancing = 0, blocked;
    float ext_eye[3];                 /* spad 38A0 (extended eye)     */
    float first_pt[3] = { 0, 0, 0 };  /* spad 38C0/3950 (1st hit)     */
    float first_n[3]  = { 0, 0, 0 };  /* spad 38E0 (1st hit normal)   */
    float pdir[3]     = { 0, 0, 0 };  /* spad 3960 (horiz player-ward
                                         sight dir)                   */
    float dir[3]      = { 0, 0, 0 };  /* spad 38A0' (unit player->eye)*/
    uint16_t attr = 0;

    /* 1. PRIMARY PROBE — player position -> eye extended 1.5 u. */
    {
        float d[3] = { eye[0] - g.pos[0], eye[1] - g.pos[1],
                       eye[2] - g.pos[2] };
        cam_norm3(d);
        ext_eye[0] = eye[0] + SOLV_EXT * d[0];
        ext_eye[1] = eye[1] + SOLV_EXT * d[1];
        ext_eye[2] = eye[2] + SOLV_EXT * d[2];
    }
    blocked = em_collision_segment_query(&g.coll, g.pos, ext_eye, mask,
                                         EM_COLL_ID_NONE, &hit);
    if (blocked) {
        int from_grid = hit.kind == EM_COLL_SET_GRID; /* hub return 4 */
        bits = 1;                     /* a plain aim block returns 1  */
        attr = hit.surf_class;
        cam->hit_attr = attr;                          /* cam+0x58 */
        memcpy(first_pt, hit.point, sizeof first_pt);
        memcpy(first_n, hit.normal, sizeof first_n);
        {
            float tdir[3] = { tgt[0] - eye[0], 0.0f, tgt[2] - eye[2] };
            cam_norm3(tdir);
            if (tdir[0] * first_n[0] + tdir[2] * first_n[2]
                    < AIMS_GLANCE_COS)
                glancing = 1;
        }
        pdir[0] = g.pos[0] - eye[0];
        pdir[2] = g.pos[2] - eye[2];
        cam_norm3(pdir);
        dir[0] = ext_eye[0] - g.pos[0];
        dir[1] = ext_eye[1] - g.pos[1];
        dir[2] = ext_eye[2] - g.pos[2];
        cam_norm3(dir);

        /* 2. FIRST STAGE — no waiver, no wedge eject. */
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN |
                    EM_SURF_FLOOR | EM_SURF_SLOPE)) {     /* 0xD800 */
            if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                if (first_pt[1] < cam->y_lo) cam->y_lo = first_pt[1];
                bits |= 8;
            } else {
                if (first_pt[1] > cam->y_hi) cam->y_hi = first_pt[1];
                bits |= 0x10;
            }
            eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
            eye[1] = first_pt[1];          /* full copy: ride the hit */
            eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
        } else {
            /* wall/other: PULL-IN at constant height — "R1 keeps the
             * pull-in" (the user's note) is engine truth. */
            eye[0] = first_pt[0] + SOLV_PULL_IN * dir[0];
            eye[2] = first_pt[2] + SOLV_PULL_IN * dir[2];
            if (from_grid) {
                float pb[3] = { first_pt[0] - dir[0],
                                first_pt[1] - dir[1],
                                first_pt[2] - dir[2] };
                cam_bounds_settle_0018CE60(cam, pb, 2);
            } else {
                if (eye[1] <= cam->y_lo) eye[1] = cam->y_lo;
                if (eye[1] >= cam->y_hi) eye[1] = cam->y_hi;
            }
        }
        /* EM_CAMERA_TRACE=1 — first-stage branch (debug). */
        {
            static int trace = -1;
            if (trace < 0)
                trace = getenv("EM_CAMERA_TRACE") != NULL;
            if (trace)
                printf("camera: frame %d 0018F870 BLOCKED attr 0x%04x "
                       "%s-> des eye %.2f %.2f %.2f (player %.2f %.2f "
                       "%.2f)\n", g.frame_no, attr,
                       glancing ? "[glancing] " : "",
                       eye[0], eye[1], eye[2],
                       g.pos[0], g.pos[1], g.pos[2]);
        }
    }

    /* 3. LATERAL STAGE (style 6 scope would skip it entirely). */
    if (blocked && !glancing) {
        /* CORNER SLIDE — a lane 1 u off the wall, -3 .. +5.5 along it. */
        float ayaw = cam_wrap_pi(atan2f(first_n[0], first_n[2])
                                 - EM_PI * 0.5f);
        float ua[3] = { sinf(ayaw), 0.0f, cosf(ayaw) }; /* unit along */
        float av[3] = { SOLV_SIDE * ua[0], 0.0f, SOLV_SIDE * ua[2] };
        float base[3] = { eye[0] + AIMS_CORNER_OFF * first_n[0],
                          eye[1] + AIMS_CORNER_OFF * first_n[1],
                          eye[2] + AIMS_CORNER_OFF * first_n[2] };
        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;
            float st[3] = { base[0] - sgn * AIMS_CORNER_BACK * ua[0],
                            base[1],
                            base[2] - sgn * AIMS_CORNER_BACK * ua[2] };
            float en[3] = { base[0] + sgn * av[0],
                            base[1],
                            base[2] + sgn * av[2] };
            if (em_collision_segment_query(&g.coll, st, en, mask,
                                           EM_COLL_ID_NONE, &hit) &&
                (hit.surf_class & EM_SURF_WALL) &&
                cam_dot3(hit.normal, first_n) < AIMS_WALL_DOT) {
                eye[0] = hit.point[0] - sgn * av[0];
                eye[2] = hit.point[2] - sgn * av[2];
                bits |= side == 0 ? 2 : 4;
                break;     /* the second lane probes only if the first
                              found nothing (engine: s2 & 2 gate) */
            }
        }
    } else if (!blocked || glancing) {
        /* SIDE STAGE — the follow solver's shape, aim variant: no
         * far-end-graze check, no confirm re-probe. */
        float syaw = cam_wrap_pi(atan2f(tgt[0] - eye[0],
                                        tgt[2] - eye[2]) - EM_PI * 0.5f);
        float su[3] = { sinf(syaw), 0.0f, cosf(syaw) };  /* unit side */
        float sv[3] = { SOLV_SIDE * su[0], 0.0f, SOLV_SIDE * su[2] };
        float lpt[3] = { 0, 0, 0 }, rpt[3] = { 0, 0, 0 };
        float cand_a[3], cand_b[3];
        int   raw_l = 0, raw_r = 0;   /* lane hit, not dot-rejected */
        memcpy(cand_a, eye, sizeof cand_a);
        memcpy(cand_b, eye, sizeof cand_b);
        for (int side = 0; side < 2; side++) {
            float sgn = side == 0 ? 1.0f : -1.0f;
            float st[3], en[3], gate;
            if (!blocked) {
                st[0] = eye[0]; st[1] = eye[1]; st[2] = eye[2];
            } else {
                st[0] = eye[0] - sgn * AIMS_CORNER_BACK * su[0];
                st[1] = eye[1];
                st[2] = eye[2] - sgn * AIMS_CORNER_BACK * su[2];
            }
            en[0] = eye[0] + sgn * sv[0];
            en[1] = eye[1];
            en[2] = eye[2] + sgn * sv[2];
            if (!em_collision_segment_query(&g.coll, st, en, mask,
                                            EM_COLL_ID_NONE, &hit))
                continue;
            gate = 0.0f;
            if (blocked) {            /* glancing-only validation */
                if (bits & 8) {       /* 1st stage was ceiling-class */
                    gate = cam_dot3(hit.normal, pdir);
                    if (gate < SOLV_CROSS_DOT) continue;
                } else {
                    gate = cam_dot3(hit.normal, first_n);
                    if (gate < SOLV_OPPOSE_DOT) continue;
                }
            }
            if (side == 0) { raw_l = 1; memcpy(lpt, hit.point, 12); }
            else           { raw_r = 1; memcpy(rpt, hit.point, 12); }
            /* response window (non-glancing: gate in (-0.3, 0.9)) */
            if (!glancing &&
                (!(gate < SOLV_GATE_HI) || gate <= SOLV_GATE_LO))
                continue;
            {
                float rd[3] = { en[0] - st[0], en[1] - st[1],
                                en[2] - st[2] };
                float c[3]  = { hit.point[0] - sgn * sv[0],
                                hit.point[1],
                                hit.point[2] - sgn * sv[2] };
                float *cand = side == 0 ? cand_a : cand_b;
                cam_norm3(rd);
                if (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
                    if (-hit.normal[1] < AIMS_WALL_DOT) {
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 2 : 4;
                    } else {
                        /* flat underside: keep the candidate's own Y
                         * (right side sets bit 8 only — never applied
                         * below, engine quirk kept) */
                        cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                        cand[1] = c[1];
                        cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                        bits |= side == 0 ? 0xA : 0x8;
                    }
                } else if (hit.surf_class & EM_SURF_WALL) {
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 2 : 4;
                } else {
                    /* other class (right side: bit 1 only, quirk) */
                    cand[0] = c[0] + SOLV_SIDE_PAD * rd[0];
                    cand[1] = c[1];
                    cand[2] = c[2] + SOLV_SIDE_PAD * rd[2];
                    bits |= side == 0 ? 3 : 1;
                }
            }
        }
        /* selection (engine: midpoint whenever the OTHER lane also hit
         * something it did not dot-reject, even window-failed) */
        if ((bits & 6) == 6 || ((bits & 2) && raw_r) ||
            ((bits & 4) && raw_l)) {
            eye[0] = 0.5f * (lpt[0] + rpt[0]);   /* corridor centering */
            eye[2] = 0.5f * (lpt[2] + rpt[2]);
        } else if (bits & 2) {
            memcpy(eye, cand_a, 12);             /* full copy, incl. Y */
        } else if (bits & 4) {
            memcpy(eye, cand_b, 12);
        }
    }

    /* 4. FINAL Y POLICY (blocked by a NON-wall class). */
    if (blocked && !(attr & EM_SURF_WALL)) {
        if (attr & (EM_SURF_CEIL | EM_SURF_STEEPDN)) {
            float lim = first_pt[1] - 1.0f;      /* stay under it */
            if (eye[1] > lim) eye[1] = lim;
        } else {
            float lim = first_pt[1] + 1.0f;      /* ride over it */
            if (eye[1] < lim) eye[1] = lim;
        }
    }

    /* 5. CONFIRM RE-PROBE — any response moved the eye: re-test the
     * player->eye line; still blocked -> park ON the new hit (x/z,
     * height kept, no pad). */
    if ((bits & 0x1F) &&
        em_collision_segment_query(&g.coll, g.pos, eye, mask,
                                   EM_COLL_ID_NONE, &hit)) {
        eye[0] = hit.point[0];
        eye[2] = hit.point[2];
    }

    /* 6. NEW BOUNDS for the NEXT frame (written, not applied here). */
    {
        float anchor[3] = { g.pos[0], g.pos[1] + SOLV_PLAYER_UP,
                            g.pos[2] };
        float d[3] = { eye[0] - anchor[0], eye[1] - anchor[1],
                       eye[2] - anchor[2] };
        float pb[3], probe[3], lo, hi;
        cam_norm3(d);
        pb[0] = eye[0] - AIMS_BOUND_PULL * d[0];
        pb[1] = eye[1] - AIMS_BOUND_PULL * d[1];
        pb[2] = eye[2] - AIMS_BOUND_PULL * d[2];
        probe[0] = pb[0];
        probe[1] = pb[1] - SOLV_BOUND_RANGE;
        probe[2] = pb[2];
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                               EM_SURF_WALL)))   /* 0x7000, verbatim */
            lo = hit.point[1] + AIMS_FLOOR_PAD;
        else
            lo = cam->y_lo - SOLV_BOUND_RANGE;
        probe[1] = pb[1] + SOLV_BOUND_RANGE;
        if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                       EM_COLL_ID_NONE, &hit) &&
            (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
            hi = hit.point[1] - SOLV_CEIL_PAD;
        else
            hi = eye[1] + SOLV_BOUND_RANGE;      /* area 0x12 variant
                                                    omitted, flagged */
        if (lo > hi) lo = hi - 3.0f;
        cam->y_lo = lo;
        cam->y_hi = hi;
    }
    return bits;
}

/* func_0018D910 — the style-5 DIRECTOR solver (decoded s64; the s61
 * table's "returns 0" undersold it): it never moves the eye — it is
 * BOUNDS MAINTENANCE ONLY, the func_0018CE60 settle shape anchored at
 * the eye pulled 1.0 toward player + 11 up: floor probe 200 down
 * (class 0x7000) -> lower = floor + 17 (+6 when cam+0x5C == 1);
 * ceiling probe 200 up (0x8800) -> upper = ceiling - 1, else eye_des.y
 * + 200 (area-0x12 player-up variant omitted, flagged); lower forced
 * to upper - 3. Keeps cam+0x50/+0x54 fresh while a director/fixed
 * camera owns the eye, so the first follow solve after release clamps
 * against live bounds. */
static void cam_solver_0018D910(EmCamera *cam)
{
    const unsigned mask = EM_COLL_SET_CELLS | EM_COLL_SET_GRID;
    float anchor[3] = { g.pos[0], g.pos[1] + SOLV_PLAYER_UP, g.pos[2] };
    float d[3] = { cam->eye_des[0] - anchor[0],
                   cam->eye_des[1] - anchor[1],
                   cam->eye_des[2] - anchor[2] };
    float pb[3], probe[3], lo, hi;
    EmCollHit hit;

    cam_norm3(d);
    pb[0] = cam->eye_des[0] - AIMS_BOUND_PULL * d[0];
    pb[1] = cam->eye_des[1] - AIMS_BOUND_PULL * d[1];
    pb[2] = cam->eye_des[2] - AIMS_BOUND_PULL * d[2];
    probe[0] = pb[0];
    probe[1] = pb[1] - SOLV_BOUND_RANGE;
    probe[2] = pb[2];
    if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                   EM_COLL_ID_NONE, &hit) &&
        (hit.surf_class & (EM_SURF_FLOOR | EM_SURF_SLOPE |
                           EM_SURF_WALL)))        /* 0x7000, verbatim */
        lo = hit.point[1] + (cam->var_5c == 1.0f ? SOLV_FLOOR_PAD1
                                                 : SOLV_FLOOR_PAD);
    else
        lo = cam->y_lo - SOLV_BOUND_RANGE;
    probe[1] = pb[1] + SOLV_BOUND_RANGE;
    if (em_collision_segment_query(&g.coll, pb, probe, mask,
                                   EM_COLL_ID_NONE, &hit) &&
        (hit.surf_class & (EM_SURF_CEIL | EM_SURF_STEEPDN)))
        hi = hit.point[1] - SOLV_CEIL_PAD;
    else
        hi = cam->eye_des[1] + SOLV_BOUND_RANGE;
    if (lo > hi) lo = hi - 3.0f;
    cam->y_lo = lo;
    cam->y_hi = hi;
}

/* func_0018D7B0 (style 0) — the desired-eye solver dispatcher.
 * Mask 6 (static cells + grid; style 2 would use 7 = + movable
 * hulls), the func_0018DD20 core above, result byte -> struct +0x07
 * (cam->hit), then the actual eye (D_008105D0) smooth-chases the
 * solved desired eye per axis capped 4.0 u/frame and the actual
 * target hard-copies the desired (func_0018C0C0). */
void camera_solve(EmCamera *cam)
{
    float eye_des[3] = { cam->eye_des[0], cam->eye_des[1], cam->eye_des[2] };

    cam->hit = 0;

    /* FIXED-CAMERA REGION: the room spec is authoritative — no wall
     * solve (the designers placed the eye), CHASE DISABLED: the actual
     * eye is a hard copy of the spec. An R1-release frame lands here
     * with the spec already re-pinned by the dispatch, so the snap-back
     * is INSTANT (one frame, no lerp) — the observed behavior. The
     * engine still runs solver STYLE 5 for director cams =
     * func_0018D910 (decoded s64): BOUNDS MAINTENANCE ONLY — keep
     * cam+0x50/+0x54 fresh so the first follow solve after release
     * clamps against live bounds, not the pre-region pair. */
    if (g.cam_region_on) {
        if (g.coll.poly_count)
            cam_solver_0018D910(cam);
        cam->eye[0] = eye_des[0];
        cam->eye[1] = eye_des[1];
        cam->eye[2] = eye_des[2];
        cam->tgt[0] = cam->tgt_des[0];
        cam->tgt[1] = cam->tgt_des[1];
        cam->tgt[2] = cam->tgt_des[2];
        return;
    }
    if (g.coll.poly_count) {
        if (!cam->aim_phase) {
            /* THE FOLLOW SOLVE — the decoded func_0018DD20, style 0
             * (mask 6), result bits -> struct +0x07. It mutates
             * cam->eye_des in place (engine: cam+0x10); the chase
             * below consumes the mutated copy. */
            cam->hit = (uint8_t)cam_solver_0018DD20(cam);
            eye_des[0] = cam->eye_des[0];
            eye_des[1] = cam->eye_des[1];
            eye_des[2] = cam->eye_des[2];
        } else {
            /* AIM camera: the engine solves mode 1 with STYLE 2 ->
             * func_0018F870 over mask 7 (movable hulls in) — DECODED
             * s64 and translated verbatim (cam_solver_0018F870 above;
             * the CAM_WALL_MARGIN stand-in is RETIRED). R1 KEEPS the
             * constant-height pull-in; the follow solver's waiver/
             * wedge/rise-over-floor responses structurally never run
             * while aiming. */
            cam->hit = (uint8_t)cam_solver_0018F870(cam);
            eye_des[0] = cam->eye_des[0];
            eye_des[1] = cam->eye_des[1];
            eye_des[2] = cam->eye_des[2];
        }
    }
    cam->eye[0] = cam_chase_h(cam->eye[0], eye_des[0], CAM_EYE_CAP);
    cam->eye[2] = cam_chase_h(cam->eye[2], eye_des[2], CAM_EYE_CAP);
    cam->eye[1] = cam_chase_v(cam->eye[1], eye_des[1], CAM_EYE_CAP);

    /* MODE-1 dispatcher tail (func_00197D20): with the eye BELOW
     * player.y + 23 and horizontally inside 8 u, push it out to
     * EXACTLY 8 along its own heading (the min-distance clamp).
     * AUDIT CORRECTION: src/func_00197D20.c case 1 reads
     *   if (D_008105D4 < 23.0f + *(float *)(arg1 + 0xA4)) { ... }
     * (D_008105D4 = the ACTUAL eye Y, arg1+0xA4 = player Y) — the
     * clamp guards the LOW eye that would otherwise slide inside the
     * player, not a high one. The port tested `>` and so applied the
     * shove in exactly the frames the engine leaves alone. */
    if (cam->aim_phase && cam->eye[1] < g.pos[1] + 23.0f) {
        float dx = cam->eye[0] - g.pos[0];
        float dz = cam->eye[2] - g.pos[2];
        float d  = sqrtf(dx * dx + dz * dz);
        if (d < CAM_AIM_MIN_DIST) {
            float h = atan2f(dx, dz);
            cam->eye[0] = g.pos[0] + sinf(h) * CAM_AIM_MIN_DIST;
            cam->eye[2] = g.pos[2] + cosf(h) * CAM_AIM_MIN_DIST;
        }
    }

    /* ACTUAL TARGET: hard copy (func_0018C0C0) — except the mode-1
     * aim chases it (0.4 u/frame entry / 0.6 steady, func_00197740/
     * func_00197870) and a live door-cinematic re-blend window
     * (cam+0xA0) chases at <= 1.0 u/frame (func_001916C0's tail). */
    if (cam->aim_phase) {
        float cap = cam->aim_phase == 1 ? 0.4f : 0.6f;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], cap);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], cap);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], cap);
    } else if (cam->tgt_soft > 0) {
        cam->tgt_soft--;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], 1.0f);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], 1.0f);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], 1.0f);
    } else {
        cam->tgt[0] = cam->tgt_des[0];
        cam->tgt[1] = cam->tgt_des[1];
        cam->tgt[2] = cam->tgt_des[2];
    }

    /* EM_CAMERA_TRACE=1 — solve outcome (the per-branch trace lives in
     * cam_solver_0018DD20). */
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_CAMERA_TRACE") != NULL;
    if (trace && cam->hit)
        printf("camera: frame %d solver bits 0x%02x (eye y %.2f -> des "
               "%.2f, bounds [%.1f, %.1f])\n", g.frame_no, cam->hit,
               cam->eye[1], eye_des[1], cam->y_lo, cam->y_hi);
}

/* func_0018C0D0(cam, 1) — the per-frame COMMIT. Engine steps:
 *   1. fwd = normalize(target - eye), degenerate-guarded;
 *   2. view position = eye + 4.0*fwd (near push; mode 0xA uses -1.0);
 *   3. func_00102CD0 look-at with up = D_008105F0 = (0,-1,0) — see
 *      em_mat4_lookat_gs for the Y-down/handedness reconciliation;
 *   4. P from zoom s, K = P*V -> every draw's matrix slot 0 (M = K*W).
 * Step 4 is the ENGINE projection (em_mat4_perspective_gs from the
 * camera's zoom field — see the "Engine projection" block above): the
 * old port 50-deg-at-window-aspect perspective with invented 0.5/500-800
 * clip planes is retired. The matrix bakes the 4:3 frame; the gfx
 * letterbox keeps that the displayed aspect at any window size. */
void camera_commit(EmCamera *cam)
{
    float dx  = cam->tgt[0] - cam->eye[0];
    float dy  = cam->tgt[1] - cam->eye[1];
    float dz  = cam->tgt[2] - cam->eye[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-3f) {  /* degenerate guard: keep the last forward */
        cam->fwd[0] = dx / len;
        cam->fwd[1] = dy / len;
        cam->fwd[2] = dz / len;
    }

    float pos[3] = { cam->eye[0] + CAM_NEAR_PUSH * cam->fwd[0],
                     cam->eye[1] + CAM_NEAR_PUSH * cam->fwd[1],
                     cam->eye[2] + CAM_NEAR_PUSH * cam->fwd[2] };
    em_mat4_lookat_gs(cam->view, pos, cam->fwd, cam->up);

    /* Commit bookkeeping (engine step 4): D_00810690 = the DESIRED
     * pair's horizontal eye<->target distance — next frame's solver
     * reads it for the head-clear waiver gate (one frame stale,
     * engine-true). */
    {
        float hx = cam->tgt_des[0] - cam->eye_des[0];
        float hz = cam->tgt_des[2] - cam->eye_des[2];
        cam->horiz_dist = sqrtf(hx * hx + hz * hz);
    }

    float proj[16];
    em_mat4_perspective_gs(proj,
                           cam->zoom > 0.0f ? cam->zoom
                                            : ENGINE_CAM_ZOOM_S);
    em_mat4_mul(g.viewproj, proj, cam->view);

    /* EM_PROJ_TEST=1 — ENGINE-PROJECTION TRUTH TEST (one-shot, quits).
     * Ground truth = the state01 PCSX2 savestate (decomp repo
     * scratch/state01): its EE RAM carries the engine's own composed
     * camera matrix K = P*V (render-ctx +0x23C0) AND the camera inputs
     * (eye D_008105D0, target D_008105E0, zoom 480), and its screenshot
     * is the rendered frame for exactly that state. The expected pixels
     * below were produced by projecting world points through THAT K and
     * mapping GS->frame (x: [1792,2304]->640, y: [1936,2160] field
     * ->480); the player-root expectation (320.0, 441.1) was visually
     * confirmed to land between the player's boots in the screenshot.
     * The check: run the savestate's eye/target/zoom through the PORT
     * chain — engine commit semantics (fwd, eye + 4*fwd near push,
     * em_mat4_lookat_gs) + em_mat4_perspective_gs + the 4:3 viewport
     * mapping — and require the same pixels to 0.05 px. This pins the
     * whole native remap (axis flips included, via the off-center
     * points) to the engine's arithmetic, not to a formula re-derivation. */
    {
        static int pt = -1;
        if (pt < 0) pt = getenv("EM_PROJ_TEST") != NULL;
        if (pt == 1) {
            pt = 2;
            static const float t_eye[3] =
                { 251.2506561f, 248.8504944f, 170.2859192f };
            static const float t_tgt[3] =
                { 218.5923157f, 246.4232635f, 201.7888184f };
            static const float t_root[3] =
                { 218.5923004f, 229.8504486f, 201.7888641f };
            /* world point -> expected 640x480 frame pixel (engine K) */
            static const float t_pt[5][5] = {
                {   0.0f, 0.0f, 0.0f, 319.9994f, 441.0799f },  /* root  */
                {   5.0f, 0.0f, 0.0f, 276.9809f, 462.2875f },  /* +5x   */
                {   0.0f, 0.0f, 5.0f, 282.2786f, 423.7764f },  /* +5z   */
                {   0.0f, 8.0f, 0.0f, 319.9994f, 345.0756f },  /* +8y   */
                { -18.5923004f, 5.1495514f, 28.2111359f,
                    272.6450f, 306.1691f },                    /* off   */
            };
            float fw[3] = { t_tgt[0] - t_eye[0], t_tgt[1] - t_eye[1],
                            t_tgt[2] - t_eye[2] };
            float fl = sqrtf(fw[0]*fw[0] + fw[1]*fw[1] + fw[2]*fw[2]);
            fw[0] /= fl; fw[1] /= fl; fw[2] /= fl;
            float tp[3] = { t_eye[0] + CAM_NEAR_PUSH * fw[0],
                            t_eye[1] + CAM_NEAR_PUSH * fw[1],
                            t_eye[2] + CAM_NEAR_PUSH * fw[2] };
            float tup[3] = { 0.0f, -1.0f, 0.0f };
            float tv[16], tpr[16], tk[16];
            em_mat4_lookat_gs(tv, tp, fw, tup);
            em_mat4_perspective_gs(tpr, ENGINE_CAM_ZOOM_S);
            em_mat4_mul(tk, tpr, tv);
            int fails = 0;
            for (int i = 0; i < 5; i++) {
                float p[4] = { t_root[0] + t_pt[i][0],
                               t_root[1] + t_pt[i][1],
                               t_root[2] + t_pt[i][2], 1.0f };
                float c[4];
                for (int r = 0; r < 4; r++)
                    c[r] = tk[0+r]*p[0] + tk[4+r]*p[1] +
                           tk[8+r]*p[2] + tk[12+r];
                float px = (1.0f + c[0]/c[3]) * 0.5f * 640.0f;
                float py = (1.0f - c[1]/c[3]) * 0.5f * 480.0f;
                float d  = c[2] / c[3];
                int ok = fabsf(px - t_pt[i][3]) <= 0.05f &&
                         fabsf(py - t_pt[i][4]) <= 0.05f &&
                         d > 0.0f && d < 1.0f;
                if (!ok) fails++;
                printf("proj test: pt%d port (%8.4f, %8.4f) d %.6f — "
                       "engine (%8.4f, %8.4f): %s\n", i, px, py, d,
                       t_pt[i][3], t_pt[i][4], ok ? "ok" : "FAILED");
            }
            printf("proj test: engine s=480 projection vs state01 "
                   "K=P*V (5 pts, 0.05 px): %s\n",
                   fails ? "FAIL" : "PASS");
            fflush(stdout);
            em_frame_request_quit();
        }
    }
}

/* DOOR-TRANSIT CINEMATIC CAMERA — DECODED + LIVE-VERIFIED (the "DOOR
 * CAMERA CUES" constants block above; two PCSX2 transits). Once the
 * walk-to arrives and the door script begins, the OPEN script's op
 * 0x0D sub 5 cue fires — a HARD CUT to 20 u behind the STAGING POINT
 * along the THROUGH-DOOR axis at +19, looking at the staging point
 * (+13) — and the camera then HOLDS that eye while the actual target
 * re-blends toward the walking player at <= 1.0 u/frame (the
 * cam+0xA0 = 120 window): the cinematic angle the door walk-through
 * plays under. When the player crosses the doorway plane (the room
 * move), the chase RE-SEATS behind the through-door pose and the
 * normal solve owns the camera again (rising over the doorframe —
 * engine-observed). The LOCKED-TRY cut (func_001BBBF0, em_door subs
 * 1/2: target at the door HANDLE — 8 u to the door's left, +10 — eye
 * 13 u back along the live camera heading at door.y + 12) runs off
 * em_door_locked_look() and HOLDS both eye and target pinned until
 * the finish script restores (the old EM_DOORCAM_LOCKED env preview
 * is retired — the real sequence drives it now). */
static void camera_door_cinematic(EmCamera *cam)
{
    float tt[3], tyaw;
    if (g.doorcam == 3)             /* post-warp (script ended, op 0x18
                                     * restored the camera): hold the
                                     * re-seated chase placement until
                                     * the fade-in unlocks */
        return;
    if (g.doorcam == 4)             /* LOCKED-LOOK hold: the engine
                                     * hard-copied once and never moves
                                     * the camera again — the finish
                                     * script's restore is handled at
                                     * the dispatcher (camera_update) */
        return;
    if (g.doorcam < 2) {
        float dp[3], dyaw, snapyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            g.doorcam = 1;          /* approach walk: hold the chase
                                     * camera still (commit only) */
            /* Latch the staging point + through-door yaw — the
             * engine's kickoff snap pose (spad 3B40/3B50), which the
             * cut below builds from. The walk-to target IS the
             * staging point and its yaw IS the front/back snap. */
            memcpy(g.doorcut_pos, tt, sizeof g.doorcut_pos);
            g.doorcut_yaw = tyaw;
            return;
        }
        /* walk-to arrived — the door script starts THIS frame: cut. */
        if (em_door_locked_look(dp, &dyaw, &snapyaw)) {
            /* func_001BBBF0 — the LOCKED-TRY cut: target = door + 8 u
             * toward the HANDLE side (the door-yaw left) + 10 up, eye
             * = target - 13 along D_00810374 with eye.y = door.y + 12.
             * Hard copy, then HOLD pinned (doorcam 4) until the finish
             * script restores.
             *
             * s71 YAW-SOURCE CORRECTION (PCSX2 ground truth: the
             * engine parks this shot in the SAME spot regardless of
             * how the camera was oriented on approach): D_00810374 is
             * NOT the live chase heading. Full writer census (every
             * main-ELF reference): only script/cutscene ops write it —
             * op01 walk-to sub (func_001B6F80 = rec[+0x34], a
             * script-authored yaw), op04 (rec angles), op15/op19
             * compounds, func_001B6F00 (actor yaw + offset, scripted-
             * actor cues), func_00183160 (+= delta, overlay-called),
             * and the room-entry mode-0xA fixed-cam armer
             * (func_001B0460 -> func_00102C58). The chase camera NEVER
             * writes it, so at the locked try it holds the last
             * SCRIPTED camera yaw — deterministic per route. The old
             * cam->yaw read only looked right before s67 (the chase
             * yaw was approach-independent after the kickoff walk);
             * the s67 tether made cam->yaw an output and broke it.
             * The port has no script-yaw global, so the deliberate,
             * orientation-independent stand-in is the THROUGH-DOOR
             * snap yaw from the kickoff (em_door's transit_yaw — the
             * same spad-3B50 anchor the open cut uses): the camera
             * parks 13 u on the player's side of the handle, facing
             * the door, from any approach (FLAGGED stand-in for the
             * last-scripted-yaw global). */
            cam->tgt_des[0] = dp[0] - LOCKCAM_HANDLE_OFF * cosf(dyaw);
            cam->tgt_des[1] = dp[1] + LOCKCAM_TGT_UP;
            cam->tgt_des[2] = dp[2] + LOCKCAM_HANDLE_OFF * sinf(dyaw);
            cam->eye_des[0] = cam->tgt_des[0]
                            - LOCKCAM_EYE_BACK * sinf(snapyaw);
            cam->eye_des[1] = dp[1] + LOCKCAM_EYE_UP;
            cam->eye_des[2] = cam->tgt_des[2]
                            - LOCKCAM_EYE_BACK * cosf(snapyaw);
            memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
            memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            cam->tgt_soft = 0;
            g.doorcam     = 4;
            return;
        }
        /* op 0x0D sub 5 (func_0018CBD0, dist -20; live-verified
         * geometry — the constants block above): 20 u behind the
         * SNAPPED pose along the THROUGH-DOOR axis at head height,
         * looking at the staging point slightly below (+13). The
         * camera heading itself snaps to the door axis (the
         * engine's cam Euler +0x30 <- spad 3B50). */
        cam->yaw = g.doorcut_yaw;
        cam->tgt_des[0] = g.doorcut_pos[0];
        cam->tgt_des[1] = g.pos[1] + DOORCAM_TGT_UP;
        cam->tgt_des[2] = g.doorcut_pos[2];
        cam->eye_des[0] = g.doorcut_pos[0]
                        - sinf(g.doorcut_yaw) * DOORCAM_EYE_BACK;
        cam->eye_des[1] = g.pos[1] + DOORCAM_EYE_UP;
        cam->eye_des[2] = g.doorcut_pos[2]
                        - cosf(g.doorcut_yaw) * DOORCAM_EYE_BACK;
        /* solver style 1 = HARD COPY desired -> actual: the cut. */
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->tgt_soft = DOORCAM_TGT_SOFT;   /* cam+0xA0 = 0x78 */
        g.doorcam     = 2;
        return;
    }
    /* ROOM-BOUNDARY RE-SEAT — DECODED s65 (func_001B0460(1) ->
     * func_001B0080(cam, 2.0), the room-entry camera re-init's
     * unflagged-record arm, called on EVERY room entry): desired
     * TARGET = the player's ground position + 17 (the idle target
     * height), desired EYE = target + rotY(through-door yaw) *
     * (0, 0, cam+0x0C) + 2 up — i.e. fabs(cam+0x0C) BEHIND the
     * through-door pose at player.y + 19 — both HARD-COPIED to the
     * actuals. Then the NORMAL dispatch + solve own the camera: the
     * eye lands inside/behind the door wall, the solver pulls in at
     * kept height and the floor-bound probe landing on the doorframe
     * lintel raises it — the live park at (104, 29, -250.4) over the
     * 21-u frame is bounds + walk handler, fully attributed (the s56
     * "+29 from func_00191000" guess stays retracted). The engine
     * reads the PER-RECORD distance (office records: -31.2); the
     * port carries only the default. Goto doors never cross the
     * plane before the fade; their re-seat is the warp re-place
     * (doorcam = 3 there, same shape). */
    {
        float dx = g.pos[0] - g.doorcut_pos[0];
        float dz = g.pos[2] - g.doorcut_pos[2];
        if (dx * sinf(g.doorcut_yaw) + dz * cosf(g.doorcut_yaw)
                > DOORCAM_PLANE) {
            g.doorcam     = 3;
            cam->yaw      = g.doorcut_yaw;
            cam->tgt_soft = 0;
            cam->swing    = 0;
            cam->tgt_des[0] = g.pos[0];
            cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
            cam->tgt_des[2] = g.pos[2];
            cam->eye_des[0] = cam->tgt_des[0]
                            - sinf(g.doorcut_yaw) * fabsf(g.cam_dist_param);
            cam->eye_des[1] = cam->tgt_des[1] + 2.0f;   /* player.y+19 */
            cam->eye_des[2] = cam->tgt_des[2]
                            - cosf(g.doorcut_yaw) * fabsf(g.cam_dist_param);
            memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
            memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            return;
        }
    }
    /* held cinematic: the eye HOLDS the cut placement; the desired
     * target tracks the player and the actual one re-blends through
     * the tgt_soft window (camera_solve's chase arm runs only outside
     * the lock, so chase here directly). */
    cam->tgt_des[0] = g.pos[0];
    cam->tgt_des[1] = g.pos[1] + DOORCAM_TGT_UP;
    cam->tgt_des[2] = g.pos[2];
    if (cam->tgt_soft > 0) {
        cam->tgt_soft--;
        cam->tgt[0] = cam_chase_h(cam->tgt[0], cam->tgt_des[0], 1.0f);
        cam->tgt[2] = cam_chase_h(cam->tgt[2], cam->tgt_des[2], 1.0f);
        cam->tgt[1] = cam_chase_v(cam->tgt[1], cam->tgt_des[1], 1.0f);
    } else {
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
    }
}

/* func_001CB590(0x008101E0, 0xD0, 0) + func_0018B9C0 — camera-context
 * begin + the camera state machine top. Runs AFTER the render-chain
 * build, like the engine; on the PS2 the already-kicked chain consumes
 * the PREVIOUS frame's matrices, while the native chain is flushed at
 * close-out with this frame's — one frame less camera latency, same
 * 60 Hz math. */
void camera_update(void)
{
    EmCamera *cam = &g.cam;

    if (cam->state == 0) {
        /* State 0 — one-shot init. Engine: vector pool setup with
         * up = (0,-1,0), then mode 8 (reposition/settle vs the world,
         * two func_0019A910 ray queries). The settle needs the collision
         * world (TODO above), so natively: snap actual = desired and
         * start the generic follow directly in mode 0. */
        cam->up[0]     = 0.0f;
        cam->up[1]     = -1.0f;  /* the engine's Y-DOWN view-up */
        cam->up[2]     = 0.0f;
        cam->top_mode  = 0;
        cam->table_sel = 1;      /* smooth dispatch table */
        cam->mode      = 0;      /* engine inits mode 8 — TODO(camera-modes) */
        cam->aim_h     = CAM_AIM_OFFSET;
        /* func_0018DD20 solver state (engine init values) */
        cam->y_lo       = -200.0f;     /* +0x50 */
        cam->y_hi       = 1000.0f;     /* +0x54 */
        cam->hit_attr   = 0;
        cam->var_5c     = 2.0f;        /* +0x5C init constant */
        cam->wall_yaw   = 0.0f;        /* +0x90 zeroed (func_0018B9C0) */
        cam->swing      = 0;           /* +0x03 walk-tether latch */
        cam->swing_yaw  = 0.0f;
        cam->horiz_dist = CAM_DIST;    /* D_00810690 pre-first-commit */
        cam->zoom      = ENGINE_CAM_ZOOM_S;  /* ctx+0x2468 default 480
                                              * (func_001D25F0; scope =
                                              * 224/tan(vfov/2) when the
                                              * top_mode-3 camera lands) */
        camera_entry_seat(cam);   /* func_001B0080 geometry — the 46.8
                                   * spawn-yaw seat the solver pulls in
                                   * from (NOT the CAM_DIST=33 stand-in) */
        memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
        memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
        cam->state     = 1;
        cam->sub_state = 0;
        /* fall through — the engine's init frame still commits */
    }
    if (cam->sub_state == 0) {   /* first run frame: 0->1 ramp */
        cam->sub_state = 1;
        cam->timer     = 0;
    }

    /* STATUS-SCREEN PAUSE: dispatch + solve are skipped and only the
     * commit runs — the engine's frozen top modes 1/2 shape. The DOOR
     * TRANSIT no longer freezes: the decoded script camera cue runs
     * instead (camera_door_cinematic — the walk-to approach holds
     * still, then the op 0x0D sub 5 cinematic cut + held angle; the
     * post-warp re-seat still happens while the screen is black,
     * gameplay_frame). */
    if (cam->top_mode == 0 && !em_hud_is_open()) {
        /* AREA-11 OPENING DIRECTOR camera (highest priority): while an
         * establishing-cutscene beat runs it OWNS the camera outright —
         * the literal eye->target keyframes (cuts hold, blends lerp). It
         * is dormant outside a beat (returns 0), so the normal chase and
         * the examine/door cues below are untouched the rest of the
         * time. On the frame the beat ends it does a one-shot chase
         * restore inside director_camera (the op18/op07-sub4 shape) and
         * returns 0, handing the camera back cleanly. */
        if (director_camera(cam)) {
            camera_commit(cam);   /* func_0018C0D0(cam, 1) */
            cam->timer++;
            return;
        }
        /* EXAMINE camera cue (em_examine.h): the script's op00 fixed
         * cut — eye from the decoded record, target framing the
         * examined object. Hard copy + hold pinned while the sequence
         * presents (the engine's cue records own the camera under
         * scripted mode); release = a one-shot chase restore behind
         * the unmoved player, the op07-sub4 restore shape exactly
         * like the locked-door finish below. Cue-less examines (the
         * snow refusal's op0D sub5 chase cue) never enter here — the
         * normal chase keeps the camera. */
        {
            float exe[3], ext[3];
            if (em_examine_camera(exe, ext)) {
                memcpy(cam->eye_des, exe, sizeof exe);
                memcpy(cam->tgt_des, ext, sizeof ext);
                memcpy(cam->eye, exe, sizeof exe);
                memcpy(cam->tgt, ext, sizeof ext);
                cam->tgt_soft = 0;
                g.examcam = 1;
                camera_commit(cam);   /* func_0018C0D0(cam, 1) */
                cam->timer++;
                return;
            }
            if (g.examcam) {
                g.examcam = 0;
                cam->tgt_soft = 0;
                cam->tgt_des[0] = g.pos[0];
                cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                cam->tgt_des[2] = g.pos[2];
                camera_desired_eye(cam);
                memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
                memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            }
        }
        if (em_door_movement_locked() && g.doorcam != 3) {
            /* pre-warp transit: approach freeze, then the script's
             * cinematic cut + held angle */
            camera_door_cinematic(cam);
        } else {
            /* doorcam 3 = post-warp WALK-OUT: the script has ended
             * (op 0x18 restored the camera at the re-place) — the
             * normal chase resumes behind the re-seated player while
             * the movement lock finishes the walk-out. */
            if (g.doorcam == 4 && !em_door_movement_locked()) {
                /* LOCKED finish (op 0x07 sub 4 -> func_001CA770):
                 * RESTORE the saved camera — the chase placement
                 * behind the unmoved player (cam->yaw was never
                 * touched by the locked cut, so desired == the
                 * pre-script chase pose; an instant copy, not a
                 * blend — engine restore semantics). */
                cam->tgt_soft = 0;
                cam->tgt_des[0] = g.pos[0];
                cam->tgt_des[1] = g.pos[1] + CAM_TGT_HEIGHT;
                cam->tgt_des[2] = g.pos[2];
                camera_desired_eye(cam);
                memcpy(cam->eye, cam->eye_des, sizeof cam->eye);
                memcpy(cam->tgt, cam->tgt_des, sizeof cam->tgt);
            }
            if (!em_door_movement_locked()) g.doorcam = 0;
            camera_prestep_00191390(cam);  /* per-state height params */
            camera_mode_dispatch(cam);   /* func_0018BC20 */
            camera_solve(cam);           /* func_0018D7B0, style 0 */
        }
    }
    /* top modes 1/2 (frozen) reach the commit only; top mode 3 is the
     * scope camera func_0022EEF0 — TODO(camera-modes). */
    camera_commit(cam);              /* func_0018C0D0(cam, 1) */
    cam->timer++;
}
