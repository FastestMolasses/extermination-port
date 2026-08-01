/* em_player.c — player locomotion, footsteps and aim direction.
 *
 * The player's own frame: wall probes and collide-and-slide, the turn-rate
 * gait ladder, the camera-relative move basis, the footstep picker with its
 * surface-attribute table, and the aim-direction ladder. Split out of em_game.c,
 * which had grown to hold the entire gameplay frame. Behaviour is unchanged by
 * the move — only the file boundary is new.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_player.h"

#include "game/em_game_internal.h"

/* func_001764E0 — the engine's RADIAL WALL PROBES (the real player
 * hitbox; see the PLAYER WALL RADIUS block above). Five directions
 * yaw + {0, +45, -45, +90, -90} deg (D_00248950), each probed twice —
 * ankle y+0.05 over the static sets and chest y+4.01 with the movable
 * hulls (doors) joined in — and every wall-class hit pushes the actor
 * back by the probe's overshoot (pos += hit - end, the spad
 * 0x700031C0 delta), exactly the engine's response. Each probe runs
 * from the ALREADY-corrected position, like the PS2 loop re-reading
 * actor +0xB0 per iteration. Runs every free/idle frame (the engine
 * fires it from both the idle top func_00161020 and the walk top
 * func_001612D0); scripted door transits skip it — the engine's
 * MOVE-TO crosses the sealed boundary planes deliberately. */
void player_wall_probes(void)
{
    static const float kProbeAngle[5] = {
        0.0f, 0.7853982f, -0.7853982f, 1.5707964f, -1.5707964f
    };                                       /* D_00248950, radians */
    if (!g.coll.poly_count)
        return;
    for (int i = 0; i < 5; i++) {
        float ang = g.yaw + kProbeAngle[i];
        float dx  = sinf(ang) * PLAYER_WALL_RADIUS;
        float dz  = cosf(ang) * PLAYER_WALL_RADIUS;
        for (int pass = 0; pass < 2; pass++) {
            float lift = pass ? PROBE_CHEST_LIFT : PROBE_ANKLE_LIFT;
            float from[3] = { g.pos[0], g.pos[1] + lift, g.pos[2] };
            float end[3]  = { from[0] + dx, from[1], from[2] + dz };
            EmCollHit hit;
            if (probe_wall_seg(from, end, pass /* doors: chest only */,
                               &hit)) {
                static int trace = -1;
                if (trace < 0) trace = getenv("EM_PROBE_TRACE") != NULL;
                if (trace)
                    printf("probe: frame %d dir %d pass %d pos (%.2f, "
                           "%.2f) hit (%.2f, %.2f) push (%.3f, %.3f)\n",
                           g.frame_no, i, pass, g.pos[0], g.pos[2],
                           hit.point[0], hit.point[2],
                           hit.point[0] - end[0], hit.point[2] - end[2]);
                g.pos[0] += hit.point[0] - end[0];
                g.pos[2] += hit.point[2] - end[2];
            }
        }
    }
}

void player_move_collide(float mx, float mz)
{
    EmCollHit hit;

    /* Integrate the move, then let the radial probes correct it (the
     * engine's order: the walk top writes +0xB0/B8, func_001764E0
     * pushes back). With the 4.5-unit radius far above the per-frame
     * step (0.3 u at run) the probes also own anti-tunneling. */
    g.pos[0] += mx;
    g.pos[2] += mz;
    player_wall_probes();

    /* Floor: vertical segment query through the same worlds (the grid
     * world owns the walkable floor — FINDINGS "COLLISION WORLD"). The
     * same class split applies downward: a leaning wall face (e.g. the
     * snow scene's gate posts, n.y slightly > 0) front-faces the probe
     * from above, and accepting it ratchets the player up the wall while
     * sliding along it — step past non-walkable crossings instead. */
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,    g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN,  g.pos[2] };
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            break;
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE) {
            g.pos[1] = hit.point[1];
            break;
        }
        if (hit.point[1] - 1e-3f <= down[1])
            break;
        from[1] = hit.point[1] - 1e-3f;
    }

    /* MOVING-SURFACE CARRY (the AREA-11 truck top — em_collision.h §11.4,
     * the PS2 truck convergence block 0x00825014). After the static floor
     * snap above, consume the per-frame moving-surface registry: if the
     * player footprint stands on a registered moving surface (the truck
     * top) within its Y band, ADD that surface's velocity to the player.
     * For the wedged truck this is vel 0 (no effect); for the FALLING truck
     * it is the accelerating downward drop — so a player lingering on top
     * is carried DOWN into the crevice (the fail), below the static floor
     * the snap resolved. The single moving-surface touch in the player
     * ground-solve (the truck registers in em_truck_update, run earlier
     * this frame). Dormant — returns 0 — whenever no actor registered. */
    em_collision_moving_carry(g.pos);

    /* STATIC BLOCKER PUSH-OUT (the AREA-11 closed GRATE — em_collision.h
     * §blocker, INVESTIGATION_area11_grate.md §5). After the static EMCL
     * wall solve (player_wall_probes) and the floor snap, eject the player
     * from any registered solid blocker AABB they have entered. The grate
     * registers its closed hull each frame ONLY while the area is NOT
     * powered (grate_update, run before actor_update), so once powered the
     * registry is empty and this is a no-op — movement away from the grate
     * is bit-for-bit unchanged (the probe returns 0 and touches nothing).
     * This is the SINGLE blocker touch in the player ground-solve, the same
     * minimal-addition shape as the moving-surface carry above. The wall
     * radius is the same (0,y,4.5) probe radius the EMCL solve uses. */
    em_collision_blocker_probe(g.pos, PLAYER_WALL_RADIUS);
}

/* player_turn_rate — func_00174AC0's banded rate select (rad/frame).
 * Picks the body-heading ease rate from whether we are turning in place
 * (current ramped speed == 0) vs. moving, then by the gait tier (in
 * place) or by |delta| crossed with the ramped speed loco_upt (moving).
 * See the BODY-HEADING TURN RATES block above for the table. */
static float player_turn_rate(int gait, float upt, float adelta)
{
    if (upt <= 0.0f) {                      /* TURN-IN-PLACE, by gait */
        if (gait == 2) return TURN_IP_GAIT2;
        if (gait == 1) return TURN_IP_GAIT1;
        return TURN_IP_GAIT03;              /* gait 0 or 3 */
    }
    if (adelta <= TURN_DELTA_BAND) {        /* MOVING, near band */
        if (upt <= 0.1f) return TURN_MV_NEAR_W;
        if (upt <= 0.3f) return TURN_MV_NEAR_J;
        return TURN_MV_NEAR_R;
    }
    if (upt <= 0.1f) return TURN_MV_FAR_W;  /* MOVING, far band */
    if (upt <= 0.3f) return TURN_MV_FAR_J;
    return TURN_MV_FAR_R;
}

/* player_turn_toward — func_001B12B0 turn-toward: ease g.yaw toward the
 * desired world heading by at most `rate` (rad/frame), snapping when
 * within one step (|delta| <= rate) so there is no overshoot / jitter.
 * Updates g.yaw in place and wraps it to [-pi, pi]. */
void player_turn_toward(float desired, float rate)
{
    float diff = desired - g.yaw;
    while (diff >  EM_PI) diff -= 2.0f * EM_PI;
    while (diff < -EM_PI) diff += 2.0f * EM_PI;
    if (diff <= rate && diff >= -rate) {
        g.yaw = desired;                    /* SNAP — within one step */
    } else {
        g.yaw += (diff > 0.0f) ? rate : -rate;
    }
    while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
    while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
}

/* player_move_cam_yaw — the engine's camera-relative move basis.
 *
 * FIX 1 (INVESTIGATION_movement_exact.md "DEEP A/B v2", live 2026-06-17):
 * the engine builds the desired heading as stickAngle + D_008106A0, where
 * D_008106A0 is the camera's HORIZONTAL VIEW yaw read from its ORIENTATION
 * BASIS — concretely the atan2 angle of the COMMITTED FORWARD vector
 * D_00810600 (FINDINGS "Global camera vector pool": "0x8106A0 atan2 angle
 * of forward"), written by the commit func_0018C0D0 right after it builds
 * forward = normalize(target - eye). It is NOT recomputed from the
 * eye/target POSITIONS each frame, and it barely rotates while moving
 * (live: ~0.004 rad/frame, ~constant over a whole run) because the
 * committed forward is the damped/smoothed camera basis — so the move
 * basis is STABLE during locomotion and the body eases onto a fixed
 * desired heading, straightening the path.
 *
 * The previous attempt returned atan2(tgt - eye) recomputed from the raw
 * actual eye/target positions. That is the EYE->TARGET geometric heading;
 * the live A/B read it at ~-2.31 rad while the engine's D_008106A0 read
 * ~+2.42 rad at the same pose. The two diverge because that expression
 * swings with the chasing eye and is recomputed from positions rather
 * than sourced from the maintained orientation.
 *
 * The faithful source is the committed forward ORIENTATION vector
 * g.cam.fwd (= engine D_00810600 = normalize(tgt - eye), with the commit's
 * degenerate "keep last forward" guard already baked in). Its XZ azimuth
 * atan2(fwd.x, fwd.z) IS the geometric quantity D_008106A0 is the atan2 of.
 * The live globals' -2.31 vs +2.42 gap is the engine-global-vs-port
 * convention offset (the engine stores D_008106A0 in its own basis and
 * pairs it with atan2(stickX,stickY); the port consumes the SAME geometric
 * forward azimuth in its atan2(x,z) basis, paired with its (fx*-sy - fz*sx)
 * stick rotation below) — feeding the engine's raw +2.42 into the port's
 * atan2(x,z) pipeline would rotate movement 90 deg / mirror it. Sourcing
 * the committed-forward azimuth keeps forward-press -> walk along the
 * camera forward AND gives the engine's stable, non-swinging basis.
 *
 * g.cam.fwd is last frame's committed forward (actor_update -> player_move
 * runs BEFORE camera_update/camera_commit), exactly as func_00174AC0 reads
 * the prior-frame D_008106A0. Degenerate guard falls back to the
 * eye->target azimuth, then the orbit yaw, so the value stays
 * deterministic if the commit has not run yet. */
float player_move_cam_yaw(void)
{
    float fx = g.cam.fwd[0];
    float fz = g.cam.fwd[2];
    if (fx * fx + fz * fz >= 1e-6f)
        return atan2f(fx, fz);       /* committed forward azimuth = D_008106A0 */
    /* commit has not produced a forward yet: fall back to the eye->target
     * azimuth (same geometric quantity), then the orbit yaw. */
    float dx = g.cam.tgt[0] - g.cam.eye[0];
    float dz = g.cam.tgt[2] - g.cam.eye[2];
    if (dx * dx + dz * dz < 1e-6f)
        return g.cam.yaw;
    return atan2f(dx, dz);
}

/* Player movement (the port's first slice of the actor spine's physics
 * side): left stick = camera-relative DESIRED heading on the XZ plane;
 * the body heading g.yaw EASES toward it at the engine's banded turn
 * rate (func_00174AC0 / func_001B12B0) and velocity is emitted ALONG
 * g.yaw — so the path curves into the move direction (the body lags the
 * stick) instead of sliding off along the raw stick instantly. With a
 * collision world loaded, movement goes through the engine's move probe
 * (walls stop/slide, the floor query sets the height); without one, the
 * old room-bbox clamp keeps the repo runnable standalone. */
void player_move(void)
{
    g.gait = 0;          /* re-quantized below; scripted paths leave 0 */

    /* DOOR TRANSIT (the engine's gameplay-frame selector 3, spad
     * 0x70003B8D, armed by the use scan): a scripted MOVE-TO carries
     * the player to the door's far-side point with yaw snapped to the
     * door normal (func_001BBE40 -> func_00182F90). Runs collision-free
     * — the doorways are statically sealed by the grid room-boundary
     * planes, and this scripted move is exactly how the engine crosses
     * them. Stick input is ignored while it runs (the selector-3 frame
     * variant does not run the free-move spine). */
    {
        float tt[3], tyaw;
        if (em_door_transit_active(tt, &tyaw)) {
            float dx   = tt[0] - g.pos[0];
            float dz   = tt[2] - g.pos[2];
            float len  = sqrtf(dx * dx + dz * dz);
            float step = WALK_SPEED * FRAME_DT;
            g.move_speed = WALK_SPEED;     /* drive the walk clip */
            g.loco_tier  = 1;              /* scripted walk = tier-1 clip */
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.yaw        = tyaw;
            if (len <= step || len < 1e-6f) {
                g.pos[0] = tt[0];
                g.pos[2] = tt[2];
            } else {
                g.pos[0] += dx / len * step;
                g.pos[2] += dz / len * step;
            }
            return;
        }
    }

    /* ARRIVAL WALK-OUT (engine player state 5/1, func_00183250 —
     * em_door.h step 4).
     * PROVENANCE (audit): func_00183250 is byte-matched but exists in
     * the decomp ONLY as an asm-void .word body (src/func_00183250.c) —
     * there is no recovered C, so the frame counts and speeds below
     * cannot be re-checked against it. Treat them as OBSERVED /
     * port stand-in until that function gets a readable decompilation,
     * not as source-derived constants.
     * After the re-place the
     * player UNINTERRUPTIBLY walks out through the door along the exit
     * yaw — 50 frames of clip-in-place (mostly under the fade-in), 30
     * frames at the locIdx-2 speed (0.3 u/tick), 30 frames decaying to
     * a stop (~12.8 u total). The stick is never read (state 5 has no
     * free-move spine); em_door owns the phases, this consumes the
     * per-frame command. Collision-free like the transit MOVE-TO (the
     * engine runs its own mover in the destination area's geometry). */
    {
        float wyaw, wspeed;
        if (em_door_walkout_active(&wyaw, &wspeed)) {
            g.yaw        = wyaw;
            /* Drive the locomotion clip at the engine's commanded tier:
             * the walk-out plays the locIdx-2 clip (family 0 -> id 2 =
             * JOG, 0.3 u/tick) even during the in-place phase. */
            g.move_speed = wspeed > 0.0f ? wspeed : GAIT_JOG_SPEED;
            g.loco_tier  = 2;
            g.loco_upt   = 0.0f;           /* free-move ramp re-arms */
            g.pos[0] += sinf(wyaw) * wspeed * FRAME_DT;
            g.pos[2] += cosf(wyaw) * wspeed * FRAME_DT;
            return;
        }
    }

    /* MOVEMENT LOCK (door transit — the decoded two-lock split,
     * em_door.h "THE TWO LOCKS"): kickoff -> walk-out end. Free
     * movement is ignored — the player walks the scripted MOVE-TO /
     * walk-out above or stands (at the staging point). The MENU lock
     * is separate (em_door_menu_locked, consumed by em_hud) and ends
     * earlier, at fade-in completion. */
    if (em_door_movement_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* scripted mode exits locomotion:
                                    * re-entry re-arms the tier ramp */
        g.loco_upt   = 0.0f;
        return;
    }

    /* EXAMINE SEQUENCE LOCK (em_examine.h): the examine script's op07
     * sub2 enters scripted mode for the whole sequence — free locomotion
     * is suppressed (the player stands), but the sequence DOES drive the
     * body heading itself: the op04 FACE pre-roll pivots the player to the
     * scripted yaw at the standing turn rate via em_game_player_face_step
     * (which calls player_turn_toward on g.yaw) BEFORE the message. The
     * engine's op01 walk-to is duration-0 for the snow terminals (the
     * use-scan already places the player within dist), so no scripted
     * translation here — only the FACE turn. We must NOT zero/seed the
     * heading here: this lock only kills move_speed/tier/upt so the floor
     * solve and anim are stand-still, while the FACE phase owns g.yaw.
     * Same lock shape as the door transit above. */
    if (em_examine_input_locked()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f;
        return;
    }

    /* SCRIPTED INTERACT / ELEVATOR RIDE LOCK (CORRECTED two-terminal
     * flow, INVESTIGATION_area11_elevator.md). Two cases share one
     * stand-still lock — the engine's "scripted-anim-owns-player" state
     * (player+0x2F3 = 3) and the powered descent both suppress free
     * movement so the script owns the player:
     *   - interact_active: a one-shot scripted clip is playing on the
     *     player (the OUTSIDE battery-insert clip 0x14, the INTERNAL
     *     lever-throw clip 0x47). Input/movement is suppressed for the
     *     clip's duration; control returns when it ends.
     *   - elev_state == 1: the platform is descending — the player
     *     stands on it and is carried DOWN by the descent's direct Y
     *     drive (elevator_tick, after actor_update), not by free
     *     movement; standing here keeps the floor-snap above from
     *     fighting that write to g.pos[1].
     * em_game_player_interact_busy() reports both as busy so the examine
     * logic does not double-trigger. FLAGGED: the original also gates
     * input via a control-mode write — modeled here as the stand-still
     * lock (no decoded control-mode value to mirror; the live decode
     * confirmed D_008101E4 stays 0 — the lock IS the scripted-anim
     * state, not a control-mode flag). elev_pending (the descent armed,
     * waiting on the lever clip) is also locked so the one frame between
     * the lever clip ending and the ride beginning (both resolved later
     * this same frame in elevator_tick) does not leak free movement —
     * exactly em_game_player_interact_busy()'s condition. */
    if (em_game_player_interact_busy()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;
        g.loco_upt   = 0.0f;
        return;
    }

    /* R2-HELD ARMED STANCE 0x1E (decoded 2026-06-11: the action machine
     * func_001607D0 dispatches HELD R2 -> player mode 0x1E, action code
     * 0x32 — the engine's SECOND aim stance, sharing the mode-1 aim
     * camera through player states 0x2A/0x29; its laser is the
     * DOT-only drawer func_001854E0 and it has no fire-counter recoil
     * — both stay with em_weapon, noted there as pending). em_game
     * runs its planted pose + steer + camera side: the held aim pose
     * through its own anim mailbox.
     *
     * FLAGGED DIVERGENCE (audit 2026-07-31) — STANCE PRIORITY IS
     * INVERTED HERE. The port gives R1 priority (the `want` gate below
     * refuses while em_weapon_is_aiming()). The recovered dispatcher
     * src/func_001607D0.c (NEARMISS — logic authoritative) gives R2
     * priority, in three places: from stance 0 it tests the R2 config
     * mask (spad 0x70003B7E) BEFORE the R1 mask (0x70003B7C); stance
     * 0x31 (R1) switches straight to 0x1E/0x32 the moment R2 is held;
     * and stance 0x32 (R2) only falls back to 0x1D/0x31 once R2 is
     * RELEASED and R1 is still held. Correcting this means suppressing
     * R1's aim while R2 is held, which lives in em_weapon.c — out of
     * this file's scope. Recorded, not fixed. */
    {
        const EmFrameInput *rin = em_frame_input();
        /* AUDIT CORRECTION (round 3 follow-up): R2 OUTRANKS R1. The
         * !em_weapon_is_aiming() term used to sit here, which gave R1
         * priority — the exact inversion of func_001607D0, whose stance
         * dispatcher tests the R2 mask BEFORE the R1 mask and whose case
         * 0x31 (R1 stance) switches to 0x1E/0x32 the moment R2 is held.
         * R1 is now suppressed while R2 is held, weapon-side, so this gate
         * no longer has to defer to it. Melee still wins over both — the
         * engine's melee states are a separate family this dispatcher is
         * not reached from. */
        int want = (rin->held & EM_PAD_R2) && !em_weapon_is_melee();
        if (want && !g.r2_aim)
            em_game_anim_hold(0x112, 1.0f);
        else if (!want && g.r2_aim && em_game_anim_active() == 0x112)
            em_game_anim_cancel();
        g.r2_aim = want;
    }

    /* PLANTED AIMING + MANUAL AIM STEER (func_0017ABA0 — the decoded
     * constants block above; retires the old AIM_TURN_SPEED turn-in-
     * place stand-in). The armed stance holds position (engine: the
     * armed modes 0x1D..0x20 replace the locomotion modes outright —
     * no aim-walk clips, zero footstep frames); the stick (and the
     * port's d-pad merge — arrows fold into the same axes, full
     * deflection) steers the aim BLENDS: pitch INVERTED-Y, yaw panning
     * the +-60 deg pose ladder first and turning the body only past
     * the blend limit. */
    {
        int aim_now = em_weapon_is_aiming() || g.r2_aim;
        if (aim_now && !g.aim_was) {
            /* STANCE ENTRY (func_0016F600 family): the aim blends reset
             * to center and the entry position is saved (D_70003040 —
             * the R2 state-0x2A camera target base), and the camera
             * arms its mode-1 entry phase. */
            g.aim_pitch = 0.5f;
            g.aim_yawb  = 0.5f;
            memcpy(g.cam.aim_entry, g.pos, sizeof g.cam.aim_entry);
            g.cam.aim_phase = 1;
        } else if (!aim_now && g.aim_was) {
            /* RELEASE (player states 0xC/0x29 -> camera mode 2): the
             * port re-seeds the chase yaw from the actual eye->player
             * heading (the engine's .L001935EC reset) and lets the
             * mode-0 chase blend back. */
            g.cam.aim_phase = 0;
            g.cam.yaw = atan2f(g.pos[0] - g.cam.eye[0],
                               g.pos[2] - g.cam.eye[2]);
        }
        g.aim_was = aim_now;
    }
    if (em_weapon_is_aiming() || g.r2_aim) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* armed modes replace locomotion */
        g.loco_upt   = 0.0f;
        const EmFrameInput *ain = em_frame_input();
        int r1fam = !g.r2_aim || em_weapon_is_aiming(); /* stance 0x31 */

        /* d-pad merge (PORT, user-attested d-pad aim): arrows act as a
         * full-deflection axis when the stick is centered. */
        int rawx = ain->lx, rawy = ain->ly;
        if (rawx == 0x80 && (ain->held & EM_PAD_LEFT))  rawx = 0x00;
        if (rawx == 0x80 && (ain->held & EM_PAD_RIGHT)) rawx = 0xFF;
        if (rawx == 0x80 && (ain->held & EM_PAD_UP))    rawy = 0x00;
        if (rawy == 0x80 && (ain->held & EM_PAD_DOWN))  rawy = 0xFF;

        /* func_001B5DC0 deflection bands + the per-stance rate rows.
         * Both rows read literally out of src/func_0017ABA0.c
         * (NEARMISS — logic authoritative): the 0x31/0x34 arm is
         * {0, 0.0025f, 0.005f, 0.015f} with f20 = 1.0f, the else arm
         * (the R2 family 0x32/0x35) is {0, 0.0016666666f, 0.005f,
         * 0.01f} with f20 = 1.5f.
         * CORRECTED (audit 2026-07-31): kRateR2[3] was 0.015f — a
         * transcription of the R1 row's top band. The recovered C
         * reads `rate[3] = 0.01f` in that arm, so the port panned the
         * R2 aim 50% too fast at full stick deflection. */
        static const float kRateR1[4] = { 0.0f, 0.0025f, 0.005f, 0.015f };
        static const float kRateR2[4] = { 0.0f, 0.0016666666f, 0.005f,
                                          0.01f };
        const float *rate = r1fam ? kRateR1 : kRateR2;
        float body_mul = r1fam ? 1.0f : 1.5f;   /* f20 */
        int bx = abs(rawx - 0x80), by = abs(rawy - 0x80);
        int bandx = bx < AIM_BAND_1 ? 0 : bx < AIM_BAND_2 ? 1
                  : bx < AIM_BAND_3 ? 2 : 3;
        int bandy = by < AIM_BAND_1 ? 0 : by < AIM_BAND_2 ? 1
                  : by < AIM_BAND_3 ? 2 : 3;

        /* YAW (+0x27C): rate scaled by 1/sin(pi*(0.5+0.6*(p-0.5))) —
         * exactly 1.0 at pitch center (the engine special-cases the
         * equality), faster pitched off level. Overflow past [0,1]
         * turns the body by the excess (screen-right = yaw decreasing
         * in the port basis, matching the engine's heading -=). */
        if (bandx) {
            float p = g.aim_pitch;
            float s = (p == 0.5f) ? 1.0f
                    : sinf(EM_PI * (0.5f + 0.6f * (p - 0.5f)));
            float step = rate[bandx] / s;
            if (rawx >= 0x80) {            /* stick RIGHT */
                g.aim_yawb -= step;        /* toward the right poses */
                if (g.aim_yawb < 0.0f) {
                    g.yaw -= -g.aim_yawb * body_mul;
                    g.aim_yawb = 0.0f;
                }
            } else {                       /* stick LEFT */
                g.aim_yawb += step;
                if (g.aim_yawb > 1.0f) {
                    g.yaw += (g.aim_yawb - 1.0f) * body_mul;
                    g.aim_yawb = 1.0f;
                }
            }
            while (g.yaw >  EM_PI) g.yaw -= 2.0f * EM_PI;
            while (g.yaw < -EM_PI) g.yaw += 2.0f * EM_PI;
        }

        /* PITCH (+0x278): INVERTED Y — stick DOWN (raw >= 0x80) raises
         * the blend = aim UP; stick UP aims DOWN ("W = down"). The R1
         * family speeds up 1.5x outside [0.3, 0.7]. */
        if (bandy) {
            float mult = (r1fam && (g.aim_pitch <= 0.3f ||
                                    g.aim_pitch >= 0.7f)) ? 1.5f : 1.0f;
            float step = rate[bandy] * mult * 0.5f;
            if (rawy >= 0x80) {            /* stick DOWN -> aim UP */
                g.aim_pitch += step;
                if (g.aim_pitch > AIM_PITCH_MAX_R1)
                    g.aim_pitch = AIM_PITCH_MAX_R1;
            } else {                       /* stick UP -> aim DOWN */
                g.aim_pitch -= step;
                if (g.aim_pitch < 0.0f) g.aim_pitch = 0.0f;
            }
        }

        /* LOCK STEER (func_0017AF70 — em_weapon.h "TARGET LOCK"):
         * with the stick idle and a target in lock slot 0, em_weapon
         * creeps the blends toward the lock at <= 0.02/frame. The
         * stick-idle gate is the engine's manual-input lock drop (the
         * 0x1D stance clears D_008106E0 whenever func_0017ABA0 flags
         * manual steering, +0x302) — the player's hand always wins. */
        if (!bandx && !bandy) {
            float lp, ly;
            if (em_weapon_lock_steer(g.pos, g.yaw, g.aim_pitch,
                                     g.aim_yawb, &lp, &ly)) {
                g.aim_pitch = lp;
                g.aim_yawb  = ly;
            }
        }
        return;
    }

    /* KNIFE / MELEE plant: the engine's melee modes 0x21/0x22 replace
     * the locomotion modes outright (em_weapon.h "KNIFE / MELEE") —
     * the player stands for the swing + recover. The heavy's in-swing
     * yaw steer (func_00173DD0, D_002486F0 rates) is untranslated
     * (flagged in em_weapon.h), so no turn-in-place here either. */
    if (em_weapon_is_melee()) {
        g.move_speed = 0.0f;
        g.loco_tier  = 0;          /* melee modes replace locomotion */
        g.loco_upt   = 0.0f;
        return;
    }

    /* ANALOG GAIT — the engine's stick quantizer func_001B5CC0 on the
     * RAW 0x80-centered bytes: r = sqrt((x-128)^2 + (y-128)^2) through
     * rings 48/88/122 -> gait byte (pad +0x17 -> player +0x23F). */
    const EmFrameInput *in = em_frame_input();
    float rdx = (float)in->lx - 128.0f;
    float rdy = (float)in->ly - 128.0f;
    float r   = sqrtf(rdx * rdx + rdy * rdy);
    int gait  = r <= GAIT_RING_1 ? 0
              : r <= GAIT_RING_2 ? 1
              : r <= GAIT_RING_3 ? 2 : 3;
    g.gait       = gait;
    g.move_speed = 0.0f;

    /* THE TIER RAMP (func_0017BC40 — the 2026-06-11 re-decode; the
     * engine's +0x38/+0x25C pair): the target is the gait's
     * D_00248870 speed; the current speed accelerates/decelerates
     * toward it through the tier boundaries, promoting/demoting
     * loco_tier as each one is crossed. */
    const float *kTier = kLocoTierSpeed;   /* +0x38/D_00248870 (shared) */
    /* Per-tier decel table D_00248890 (§2.3), indexed by current tier:
     * tier 1 -> 0.05, tier 2 -> 0.025, tier 3 -> 0.0227273. (tier 0 is
     * unused — decel only runs when loco_upt > target, i.e. tier >= 1.) */
    static const float kDecel[4] = { 0.0f, 0.05f, 0.025f, 0.0227273f };
    if (gait == 0) {                  /* dead ring: idle / run-down */
        /* RUN-DOWN applies to TIER 3 ONLY (func_0017BC40 §2.6): on stick
         * release, tiers <= 2 (walk/jog) STOP INSTANTLY (phase 3,
         * +0x38 = 0 that frame); only tier 3 (run) bleeds down (phase 2)
         * at 0.03125 u/tick/frame, CASCADING tier-by-tier to each demote
         * floor (run -> jog -> walk -> stop), carrying ~9 u. */
        if (g.loco_upt > 0.0f && g.loco_tier >= 3) {
            /* RUN-DOWN (func_0017BC40 phase 2, C2): the carried run speed
             * bleeds 0.03125 u/tick per frame and CASCADES through every
             * tier, demoting loco_tier at each tier's lower-speed FLOOR,
             * not stopping at the first boundary. (The engine x2's the
             * decay to 0.0625 when carrying gear via actor +0x314 & 0x1F;
             * the port has NO gear-carry state, so the x2 is OMITTED —
             * flag.) Velocity is emitted along the held body heading
             * g.yaw (no stick = no desired heading, so g.yaw just holds). */
            g.loco_upt -= GAIT_RUNDOWN;   /* no gear-carry x2 in the port */
            /* Cascade the tier down past each floor we drop below. */
            while (g.loco_tier > 0 && g.loco_upt <= kTier[g.loco_tier - 1])
                g.loco_tier--;
            if (g.loco_upt <= 0.0f) {     /* bled to a full stop */
                g.loco_tier = 0;
                g.loco_upt  = 0.0f;
            } else {
                g.move_speed = g.loco_upt * 60.0f;
                float rx = sinf(g.yaw), rz = cosf(g.yaw);
                if (g.coll.poly_count)
                    player_move_collide(rx * g.move_speed * FRAME_DT,
                                        rz * g.move_speed * FRAME_DT);
                else {
                    g.pos[0] += rx * g.move_speed * FRAME_DT;
                    g.pos[2] += rz * g.move_speed * FRAME_DT;
                }
                return;
            }
        } else {
            /* INSTANT STOP for tiers <= 2 (and the already-stopped case):
             * walk/jog kill speed the frame the stick is released. */
            g.loco_tier  = 0;
            g.loco_upt   = 0.0f;
            g.move_speed = 0.0f;

            /* TURN-IN-PLACE (func_00174AC0, §2.5/2.7): a stick nudge inside
             * the r <= 48 gait-0 ring still EASES the body heading toward
             * atan2(stickX,stickY) + cameraYaw at the in-place rate
             * 22.5 deg/frame (TURN_IP_GAIT03), with NO translation. Only
             * runs when the stick is actually deflected (raw magnitude above
             * a tiny epsilon, within the ring) and the player is idle (this
             * is the not-run-down path). */
            if (r > 1e-4f) {
                /* Camera-relative DESIRED heading — the BYTE-FAITHFUL engine
                 * closed form (INVESTIGATION_movement_exact.md "FORWARD-
                 * DIRECTION RESOLUTION (definitive, static)", 2026-06-17):
                 *   D_008106A0 = atan2(-fwd.z, fwd.x)  [func_0018C0D0]
                 *   stickAngle = atan2(sx, -sy)        [func_00174AC0 +0x24C]
                 *   desired    = wrap( stickAngle + pi + D_008106A0 )
                 * and the identity atan2(x,z) == atan2(-z,x) + pi/2 (dev 1e-15)
                 * collapses pi + D_008106A0 to player_move_cam_yaw() + pi/2,
                 * giving the disasm-exact closed form below. Cardinals (with
                 * cam_yaw = atan2(fwd.x,fwd.z) = player_move_cam_yaw()):
                 *   FWD  (sx=0,  sy=-1): atan2(0,1)=0      -> cam_yaw + pi/2
                 *   RIGHT(sx=1,  sy=0) : atan2(1,0)=+pi/2  -> cam_yaw + pi
                 *   BACK (sx=0,  sy=+1): atan2(0,-1)=pi    -> cam_yaw - pi/2
                 *   LEFT (sx=-1, sy=0) : atan2(-1,0)=-pi/2 -> cam_yaw
                 * The prior "door-regression" form (cam_yaw + atan2(-sx,-sy))
                 * was X-mirrored AND missing the +pi/2 — 90 deg off on the
                 * axes, 180 deg off on the FWD-RIGHT diagonal. RESTORED. */
                float sx = rdx / r, sy = rdy / r;
                float desired = atan2f(sx, -sy) + player_move_cam_yaw()
                              + (float)EM_PI * 0.5f;
                while (desired >  EM_PI) desired -= 2.0f * EM_PI;
                while (desired < -EM_PI) desired += 2.0f * EM_PI;
                player_turn_toward(desired, TURN_IP_GAIT03);
            }
        }
        player_wall_probes();         /* the idle top probes too */
        return;
    }

    /* Stick direction (normalized) -> camera-relative DESIRED heading.
     * Camera basis on XZ: forward f points from the eye towards the
     * player, screen-right is f x up = (-fz, 0, fx). Stick up walks
     * away from the camera. The camera yaw is the engine's D_008106A0
     * horizontal VIEW yaw (player_move_cam_yaw — C3), NOT the pitched
     * eye->target heading cam+0x44, so the desired heading matches the
     * engine and the body does not over-curve. This is the DESIRED
     * heading (func_001B12B0's target); the BODY heading g.yaw is eased
     * toward it below and the velocity is emitted ALONG g.yaw — KEEP
     * this camera-relative derivation, change only what we move along. */
    /* Camera-relative DESIRED heading (func_001B12B0's target) — the
     * BYTE-FAITHFUL engine closed form (INVESTIGATION_movement_exact.md
     * "FORWARD-DIRECTION RESOLUTION (definitive, static)", 2026-06-17;
     * same derivation as the turn-in-place site above):
     *   desired = atan2(sx, -sy) + player_move_cam_yaw() + pi/2   (wrapped)
     * = wrap( stickAngle + pi + D_008106A0 ) with D_008106A0 =
     * atan2(-fwd.z, fwd.x) = player_move_cam_yaw() - pi/2. So forward-press
     * (sx=0,sy=-1) heads cam_yaw + pi/2 (90 deg off the camera-forward
     * azimuth — a UNIVERSAL engine offset, disasm-proven over 3e5 random
     * orientations, max dev 1e-15); RIGHT -> cam_yaw + pi; BACK ->
     * cam_yaw - pi/2; LEFT -> cam_yaw. The prior X-mirrored, +pi/2-missing
     * "door-regression" form is REPLACED — it was 90 deg off on the axes,
     * 180 deg off on the FWD-RIGHT diagonal. Velocity is still emitted
     * along the eased body heading g.yaw (func_00178B90, unchanged). */
    float sx = rdx / r, sy = rdy / r;
    float desired = atan2f(sx, -sy) + player_move_cam_yaw()
                  + (float)EM_PI * 0.5f;
    while (desired >  EM_PI) desired -= 2.0f * EM_PI;
    while (desired < -EM_PI) desired += 2.0f * EM_PI;

    /* STANDING-ENTRY TURN-IN-PLACE GATE + TIER-0 RAMP ENTRY
     * (FIX 2 + FIX 3, INVESTIGATION_movement_exact.md "DEEP A/B v2", live
     * 2026-06-17). The earlier attempt turned in place for exactly ONE
     * frame, then immediately set loco_tier=gait-1 / loco_upt=0.3 and
     * translated while still badly misaligned — the "curves out of the
     * gate" and "snaps with no build-up" the owner sees. The live A/B
     * shows the engine instead:
     *   Phase 1 (FIX 3): while the desired heading differs from the body
     *     by more than one in-place step (22.5 deg = TURN_IP_GAIT03), it
     *     rotates IN PLACE at exactly 22.5 deg/frame with speed +0x38 = 0,
     *     tier +0x25C = 0, NOT translating, for ceil(|dHeading|/22.5deg)
     *     frames. Only once within 22.5 deg of desired does it commit.
     *   Phase 2 (FIX 2): on commit it enters the ramp from TIER 0 / speed
     *     ~0 and walks the FULL table {0,0.1,0.3,0.8} (the static
     *     "entry gait-1/speed 0.3" decode was WRONG — live ramps from
     *     zero), promoting 0->1->2->3 via the existing tier-promote loop.
     */
    int entry = (g.loco_tier == 0 && g.loco_upt <= 0.0f);
    if (entry) {
        /* wrapped |desired - g.yaw| (the engine's turn delta). */
        float adiff = desired - g.yaw;
        while (adiff >  EM_PI) adiff -= 2.0f * EM_PI;
        while (adiff < -EM_PI) adiff += 2.0f * EM_PI;
        if (adiff < 0.0f) adiff = -adiff;

        if (adiff > TURN_IP_GAIT03) {
            /* PHASE 1 (FIX 3): not yet aligned — turn in place at
             * 22.5 deg/frame with speed pinned 0, do NOT set the entry
             * tier/speed and do NOT translate this frame. The player
             * remains a standing entry (loco_tier=0, loco_upt=0), so the
             * next frame re-enters here until the body is within one step
             * of desired. (Mirrors func_00174AC0 running the turn with
             * +0x38 still 0 across all the misaligned frames, not just
             * the first.) */
            player_turn_toward(desired, TURN_IP_GAIT03);
            g.move_speed = 0.0f;
            g.loco_tier  = 0;
            g.loco_upt   = 0.0f;
            player_wall_probes();     /* the idle top probes (no translate) */
            return;
        }

        /* PHASE 2 (FIX 2): aligned within one in-place step — close the
         * remaining gap (the in-place snap) and COMMIT to the ramp from
         * TIER 0 / speed 0. The tier ramp below walks the full table. */
        player_turn_toward(desired, TURN_IP_GAIT03);
        g.loco_tier = 0;
        g.loco_upt  = 0.0f;
    }

    /* THE TIER RAMP (func_0017BC40). On the entry frame this is the first
     * accel step from TIER 0 / speed 0 (FIX 2: the entry just committed at
     * loco_tier=0, loco_upt=0, so this adds the tier-0 accel 0.05 -> 0.05
     * and walks the full {0,0.1,0.3,0.8} table over the following frames);
     * on later frames it ramps from the carried speed (a mid-run gait
     * change ramps in place). */
    {
        float target = kTier[gait];
        if (g.loco_upt < target) {            /* sub 1: accelerate */
            float acc = g.loco_tier <= 0 ? GAIT_ACCEL_0
                      : g.loco_tier == 1 ? GAIT_ACCEL_1 : GAIT_ACCEL_2;
            g.loco_upt += acc;
            if (g.loco_tier < 3 && g.loco_upt >= kTier[g.loco_tier + 1]) {
                g.loco_upt = kTier[g.loco_tier + 1];
                g.loco_tier++;                /* +0x25C += 1 */
            }
            if (g.loco_upt > target) g.loco_upt = target;
        } else if (g.loco_upt > target) {     /* sub 2: decelerate */
            /* Per-tier decel D_00248890[loco_tier] (§2.3): tier 1 = 0.05,
             * tier 2 = 0.025, tier 3 = 0.0227273 — indexed by CURRENT tier
             * (previously tier 1 wrongly shared tier 2's 0.025). */
            float dec = kDecel[g.loco_tier];
            g.loco_upt -= dec;
            if (g.loco_tier > 0 && g.loco_upt <= kTier[g.loco_tier - 1]) {
                g.loco_upt = kTier[g.loco_tier - 1];
                g.loco_tier--;                /* +0x25C -= 1 */
            }
            if (g.loco_upt < target) g.loco_upt = target;
        } else {
            g.loco_tier = gait;               /* at tier: sustained */
        }
    }

    /* STEADY-STATE TURN (subsequent move frames): ease the body heading
     * toward the desired heading at the moving band (func_00174AC0 rate
     * select + func_001B12B0 turn-toward), THEN move along the eased
     * g.yaw. Turning first and translating along the lagged body heading
     * is what makes the motion CURVE into turns instead of sliding off the
     * raw stick. The rate is banded by gait / ramped speed / |delta|. The
     * entry frame already turned in place above (C4) — do not turn twice. */
    if (!entry) {
        float diff = desired - g.yaw;
        while (diff >  EM_PI) diff -= 2.0f * EM_PI;
        while (diff < -EM_PI) diff += 2.0f * EM_PI;
        float rate = player_turn_rate(gait, g.loco_upt, fabsf(diff));
        player_turn_toward(desired, rate);
    }

    /* The ramped speed drives this frame (sustained: gait 1 = WALK
     * 6 u/s, gait 2 = JOG 18 u/s, gait 3 = RUN 48 u/s). VELOCITY IS
     * EMITTED ALONG g.yaw (the eased body heading), NOT the raw stick
     * vector (mx, mz) — the body lags the stick, so the path curves. */
    g.move_speed = g.loco_upt * 60.0f;
    float vx = sinf(g.yaw), vz = cosf(g.yaw);

    if (g.coll.poly_count) {
        player_move_collide(vx * g.move_speed * FRAME_DT,
                            vz * g.move_speed * FRAME_DT);
    } else {
        g.pos[0] += vx * g.move_speed * FRAME_DT;
        g.pos[2] += vz * g.move_speed * FRAME_DT;
        if (g.pos[0] < kRoomMin[0]) g.pos[0] = kRoomMin[0];
        if (g.pos[0] > kRoomMax[0]) g.pos[0] = kRoomMax[0];
        if (g.pos[2] < kRoomMin[1]) g.pos[2] = kRoomMin[1];
        if (g.pos[2] > kRoomMax[1]) g.pos[2] = kRoomMax[1];
        g.pos[1] = 0.0f;  /* flat floor (no collision world loaded) */
    }

    /* TODO(stop-skid/pivot, FINDINGS "GROUND LOCOMOTION"): the engine's
     * mode-6 stop-skid anims (ids 4/5) and the pivot / 180-deg about-face
     * are OUT OF SCOPE here — they need clips 4/5 re-exported into
     * player.emdl before they can play, and the about-face is its own
     * mode. The facing ease above is the steady-turn model only. */
}

/* rand5 — func_00179B90. PORT NOTE: a private deterministic LCG (ANSI
 * minimal-standard constants, high bits) stands in for the EE libc
 * rand() the engine draws from, so the self-tests reproduce run to
 * run; the engine never seeds rand either. */
unsigned footstep_rand5(void)
{
    static uint32_t s = 0x00187350u;   /* seed: the slice's own vaddr */
    s = s * 1103515245u + 12345u;
    unsigned v = (s >> 16) & 7u;
    return v >= 5u ? v - 5u : v;
}

/* Floor surface attr — the footing update func_00175900's attr copy:
 * after its own down-probe hits, the engine stores the collision
 * result record's surface-attr byte (+0x1A of the grid poly node) in
 * actor +0x23A every frame; a probe miss writes 0. The port probes at
 * step time instead (same result for a grounded player), reusing the
 * player height-resolve probe walk: step past non-walkable crossings,
 * take the first FLOOR/SLOPE hit's attr (EmCollHit.attr — the native
 * mirror of the poly node's +0x1A). No collision world -> attr 0,
 * which footstep_block maps to the default block 0x10 anyway. (The
 * movable-object override — standing on a crate forces attr 2/4 — and
 * the 0x5A/0x5B/0x5C first-contact one-shots are untranslated.) */
static uint8_t footstep_floor_attr(void)
{
    if (!g.coll.poly_count) return 0;
    float from[3] = { g.pos[0], g.pos[1] + FLOOR_PROBE_UP,   g.pos[2] };
    float down[3] = { g.pos[0], g.pos[1] - FLOOR_PROBE_DOWN, g.pos[2] };
    EmCollHit hit;
    for (int i = 0; i < 8; i++) {
        if (!em_collision_segment_query(&g.coll, from, down,
                                        EM_COLL_SET_CELLS |
                                        EM_COLL_SET_GRID, 0, &hit))
            return 0;                  /* probe miss: +0x23A = 0 */
        if (hit.surf_class == EM_SURF_FLOOR ||
            hit.surf_class == EM_SURF_SLOPE)
            return hit.attr;
        if (hit.point[1] - 1e-3f <= down[1])
            return 0;
        from[1] = hit.point[1] - 1e-3f;
    }
    return 0;
}

/* BLOCK(attr) — func_00182430's compiled-in material bases (FINDINGS
 * table; stride 0x11 = 17 ids per material: 3 tier sub-bases x 5
 * variants + 2 spare landing/scuff slots). */
static unsigned footstep_block(uint8_t attr)
{
    switch (attr) {
    case 1:    return 0x21u;
    case 2:    return 0x32u;
    case 3:    return 0x43u;
    case 4:    return 0x54u;
    case 5:    return 0x65u;
    case 6: case 7: return 0xA9u;
    case 8:    return 0x87u;
    case 0xD:  return 0xDCu;
    case 0xE:  return 0xEDu;
    case 0x5A: return 0x76u;  /* wet/puddle surface */
    case 0x5B: return 0xBAu;  /* water SHALLOW; DEEP (0xCB) needs the
                               * +0x23C depth state — untranslated */
    case 0x5C: return 0x98u;
    default:   return 0x10u;  /* attr 0 + any unmapped material — the
                               * office floor (grid attr 0, FINDINGS
                               * office cross-check) */
    }
}

/* One footstep at `tier` (the engine mapper's a1 = +0x25C; the locomotion paths
 * pass actor +0x25C, the melee impact gates a scripted 1..3).
 * EM_STEP_TRACE=1 prints each step's resolved attr/ids (debug). */
void footstep_play(int tier)
{
    uint8_t  attr = footstep_floor_attr();
    unsigned sub  = tier == 3 ? 0xAu : tier == 2 ? 5u : 0u;
    unsigned surf = footstep_block(attr) + sub + footstep_rand5();
    unsigned gear = EM_SFX_STEP_GEAR_BASE + footstep_rand5();
    static int trace = -1;
    if (trace < 0) trace = getenv("EM_STEP_TRACE") != NULL;
    if (trace)
        printf("step: f%d tier %d attr 0x%02X -> surface 0x%03X gear 0x%03X\n",
               g.frame_no, tier, attr, surf, gear);
    /* engine: positional play_sound(actor, id, 0) radius 300 for both
     * layers (FINDINGS footstep decode) — source = the player, so the
     * gains come out center/full by the play_sound math itself */
    em_sfx_play_at(surf, g.pos, 300.0f);
    em_sfx_play_at(gear, g.pos, 300.0f);
}

/* Cyclic edge test: did the looping clip playhead cross `trig` going
 * prev -> cur (both in frames, cur may have wrapped past 0)? */
int step_crossed(double prev, double cur, double trig)
{
    if (cur == prev) return 0;                    /* standing: frozen */
    if (cur > prev) return prev < trig && trig <= cur;
    return trig > prev || trig <= cur;            /* wrapped the loop */
}

/* AIM POSE LADDER blend (the dispatch half of the func_0017ABA0 steer
 * — anim_slot_index/anim_matrix_dispatch sampling the D_00248B70 sub-0
 * ladder 0x112..0x11A by the blends +0x278/+0x27C, two-buffer blend
 * func_00179CA0). The port evaluates the bilinear 3x3 pose grid at the
 * shared playhead and lerps the palettes (the same matrix-lerp the
 * port's idle/walk crossfades use — a documented stand-in for the
 * engine's bone-channel blend). Returns 1 when it produced the
 * palette (all needed ladder clips present), 0 to fall back to the
 * plain base-clip evaluation. */
int aim_ladder_eval(double t)
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    int   up = p >= 0.5f;
    int   rt = yb < 0.5f;                  /* screen-right column */
    float wp = up ? (p - 0.5f) * 2.0f : (0.5f - p) * 2.0f;
    float wy = rt ? (0.5f - yb) * 2.0f : (yb - 0.5f) * 2.0f;

    unsigned id01 = rt ? 0x115 : 0x116;            /* level, yawed   */
    unsigned id10 = up ? 0x113 : 0x114;            /* pitched, ahead */
    unsigned id11 = up ? (rt ? 0x117 : 0x119)      /* corner          */
                       : (rt ? 0x118 : 0x11A);
    struct { unsigned id; float w; } s[4] = {
        { 0x112, (1.0f - wp) * (1.0f - wy) },
        { id01,  (1.0f - wp) * wy },
        { id10,  wp * (1.0f - wy) },
        { id11,  wp * wy },
    };
    uint32_t n = g.model.bone_count * 16;
    int      first = 1;
    for (int i = 0; i < 4; i++) {
        if (s[i].w <= 0.0f) continue;
        int ci = em_model_clip_index(&g.model, s[i].id);
        if (ci < 0) return 0;              /* old EMDL: no ladder bake */
        em_model_palette_at(&g.model, (uint32_t)ci, t, g.aim_palette);
        if (first) {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] = g.aim_palette[k] * s[i].w;
            first = 0;
        } else {
            for (uint32_t k = 0; k < n; k++)
                g.player_palette[k] += g.aim_palette[k] * s[i].w;
        }
    }
    return !first;
}

/* The analytic aim direction the camera (and the self-tests) consume —
 * the pose the ladder blend selects, expressed as a world ray (the
 * engine reads the equivalent from the posed hand matrix, gun+0xC0).
 * Pose pitches/yaws are the values MEASURED from the baked ladder
 * clips (constants block above). */
void aim_dir_get(float out[3])
{
    float p  = g.aim_pitch, yb = g.aim_yawb;
    float pit_deg = p >= 0.5f
        ? AIM_POSE_CTR_DEG + (p - 0.5f) * 2.0f *
              (AIM_POSE_UP_DEG - AIM_POSE_CTR_DEG)
        : AIM_POSE_CTR_DEG - (0.5f - p) * 2.0f *
              (AIM_POSE_DOWN_DEG + AIM_POSE_CTR_DEG);
    /* yaw blend: 0 = screen right = yaw DECREASING in the port basis */
    float yaw = g.yaw + (yb - 0.5f) * 2.0f *
                (AIM_POSE_YAW_DEG * EM_PI / 180.0f);
    float pit = pit_deg * EM_PI / 180.0f;
    out[0] = sinf(yaw) * cosf(pit);
    out[1] = sinf(pit);
    out[2] = cosf(yaw) * cosf(pit);
}
