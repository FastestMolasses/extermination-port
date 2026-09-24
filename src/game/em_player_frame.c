/* em_player_frame.c — player stage of the world frame.
 *
 * Moved unchanged out of em_game.c in step S5 of
 * docs/SCENE_COORDINATOR_DESIGN.md (tools/split_module.py): actor_update,
 * the port's slice of func_0015BCF0, with its helpers. New in S5: the
 * stage function em_player_0015BCF0 (wraps the calls gameplay_frame made
 * at that position). Behaviour is unchanged by the move — only the file
 * boundary is new. Since S10a the scene core em_sf_001AE5E0 calls it
 * through the bindings (em_scene_bindings.c w_0015BCF0), between their
 * 001CB590(player) and 001CB5A0 workers.
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_game.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "em_gfx.h"
#include "em_input.h"
#include "em_math.h"
#include "em_model.h"
#include "game/em_bgm.h"
#include "game/em_collision.h"
#include "game/em_door.h"
#include "game/em_enemy.h"
#include "game/em_examine.h"
#include "game/em_frame.h"
#include "game/em_hud.h"
#include "game/em_pickup.h"
#include "game/em_sfx.h"
#include "game/em_task.h"
#include "game/em_truck.h"
#include "game/em_weapon.h"
#include "game/em_game_internal.h"
#include "game/em_effect_color.h"
#include "game/em_random.h"
#include "game/em_director.h"
#include "game/em_player.h"
#include "game/em_player_damage.h"
#include "game/em_camera.h"
#include "game/em_scene.h"
#include "game/em_props.h"
#include "game/em_opening_runtime.h"
#include "game/em_opening_actor.h"
#include "game/em_snow_runtime.h"
#include "game/em_area11_effect_runtime.h"
#include "game/em_opening_control_test.h"
#include "game/em_scene_bindings.h"

/* loco_clip_for_tier — the mode-1 id row {0,1,2,3} (idle/walk/jog/run)
 * indexed by the ramped locIdx +0x25C (func_0017B490 row D_00248A10),
 * with the same down-row fallback the asset-availability check uses:
 * tier>=3 -> run, tier>=2 -> jog, else walk; missing clips drop a row.
 * Used both for the active clip and (tier+1) for the C1 ramp cross-blend. */
static int loco_clip_for_tier(int tier)
{
    if (tier >= 3 && g.clip_run >= 0) return g.clip_run;
    if (tier >= 2 && g.clip_jog >= 0) return g.clip_jog;
    return g.clip_walk;
}

/* func_0015BCF0 — player actor update. The engine's per-actor spine
 * (state/AI, anim-evaluator selection, physics, sound triggers); the
 * port's slice of it is movement (frame input -> position/yaw) plus the
 * anim side: evaluate the bone palette at the current clip time, then
 * compose the world placement onto it.
 *
 * Idle<->walk crossfade (the anim-evaluator slice): the walk blend
 * weight seeks move_speed / WALK_SPEED linearly over ANIM_BLEND_TIME,
 * and the in-place walk clip advances at move_speed / WALK_CLIP_SPEED
 * so the stride tracks the ground (it freezes while standing). At
 * weight 0 the idle path is bit-exactly the old single-clip evaluation,
 * keeping EM_CAPTURE idle output stable.
 *
 * SCRIPTED ANIM (em_game.h em_game_anim_request): the commit slice of
 * the spine. If the request mailbox (sa_req, the native +0x1F2)
 * differs from the committed id (sa_cur, +0x20C), commit it — the
 * engine's func_00183090 "copy +0x1F2 -> +0x20C, anim_clip_init" path,
 * one frame after the request, exactly the original latency (requests
 * land from the world-services slot AFTER this update ran). While
 * committed, the scripted clip OWNS the palette: it plays once at
 * sa_rate frames/tick, holds its last frame, and ends back into
 * locomotion (clip over, or em_game_anim_cancel — the script
 * teardown's +0x1F2 = 0). The locomotion blend state is parked at
 * idle while suspended, so the return is the plain idle pose (the
 * door sequence re-places the player standing). */
static void actor_update(void)
{
    float previous_position[3] = {g.pos[0], g.pos[1], g.pos[2]};
    float previous_yaw = g.yaw;
    if (g.pd_state != 2 && player_states_stage_live()) {
        /* Census L01: the original player stage (em_player.c
         * player_states_stage): 0015BA50 advances the display source by
         * +34, the takeover stand-in may consume the stage at 0015B130's
         * prelude position, 0015B130 runs 0021C440 / the port's idle/walk
         * callbacks / the +20E countdown / 0015D100 / 0015D000, then
         * 0015BA50's tail and 0015BCF0's -200 check, loop-sound stop and
         * skeleton evaluation. A stage the takeover consumed or a
         * translated routine owned displays the record's evaluated pose
         * (player_states_record_display); the port's idle/walk callbacks
         * keep the display below until L12. */
        int consumed = player_states_stage();
        if (player_states_record_display()) {
            if (player_pose_display() < 0)
                player_pose_invalidate("record skeleton is not finite");
            return;
        }
        if (consumed != 0) return;
    } else {
        /* Since L01 the app reaches this branch only while the legacy
         * bug-latch struggle holds the player (g.pd_state == 2) or in a test
         * harness that leaves STAGE unbound: a failed stage bind latches a
         * scene fault at 0x0015BA50 (em_scene_bindings.c w_001AFCA0).
         * 0015BA50 advances source channels before 0015B130 can take ownership.
         * An accepted shared callback consumes this player stage completely. */
        if (player_pose_stage() != 0) return;
        previous_position[0] = g.pos[0];
        previous_position[1] = g.pos[1];
        previous_position[2] = g.pos[2];
        previous_yaw = g.yaw;
        /* The legacy bug-latch struggle (em_player_damage.c, a stand-in for
         * the +4 = 2 +5 = 0xD reaction 002208C0 and its death) replaces the
         * free-move spine entirely while it holds the player; the committed
         * clip owns the palette through the scripted-anim path below. No
         * AREA11 owner latches onto the player. */
        if (g.pd_state == 2) {
            player_hurt_tick();
            g.gait       = 0;
            g.move_speed = 0.0f;
            g.loco_tier  = 0;
            g.loco_reentry.phase = 0;
        } else {
            player_move();
        }
    }
    player_pose_finish_state();
    if (!g.mesh) return;

    /* scripted-anim COMMIT (func_00183090: +0x1F2 != +0x20C). */
    if (g.sa_req != g.sa_cur) {
        g.sa_cur  = g.sa_req;
        g.sa_clip = g.sa_req
                  ? em_model_clip_index(&g.model, (uint32_t)g.sa_req)
                  : -1;
        g.sa_hold = g.sa_req_hold;
        g.sa_t    = 0.0;
    }
    if (g.sa_cur && g.sa_clip >= 0) {
        const EmModelClip *cs = &g.model.clips[g.sa_clip];
        /* Evaluate, then advance; clamp at the LAST frame (one-shot —
         * em_model_palette_at would loop-blend back into frame 0). */
        double end = (double)(cs->frame_count - 1);
        double t   = g.sa_t < end ? g.sa_t : end;
        /* AIM POSE LADDER: while the held clip is the ladder BASE
         * (0x112 — the stance tops request the base code and the
         * DISPATCH picks the ladder step, exactly the engine split)
         * and the player is aiming, the bilinear pose-grid blend owns
         * the palette; the recoil restart (sa_t rewind) runs through
         * unchanged — every ladder step is the same 25 frames. */
        if (!(g.sa_cur == 0x112 &&
              (em_weapon_is_aiming() || g.r2_aim) &&
              aim_ladder_eval(t)))
            em_model_palette_at(&g.model, (uint32_t)g.sa_clip, t,
                                g.player_palette);
        palette_apply_placement(g.player_palette, g.model.bone_count,
                                g.pos, g.yaw);
        g.sa_t += (double)g.sa_rate;
        if (g.sa_t >= end + 1.0) {     /* played through: clip-end flag */
            if (g.sa_hold) {
                g.sa_t = end;          /* HELD POSE: clamp and keep the
                                        * palette (em_game_anim_hold) */
            } else {
                g.sa_req = g.sa_cur = 0;  /* (+0x200 & 0x1000 analog) */
                g.sa_clip = -1;
            }
        }
        g.walk_w = 0.0f;               /* locomotion parked at idle */
        g.idle_timer = IDLE_FIDGET_FRAMES;   /* scripted anim leaves
                                              * mode 0: cycle re-armed */
        g.idle_phase = 0;
        g.fid_w      = 0.0f;
        return;
    }

    if (player_pose_foot_stop_palette() != 0) return;

    if (g.loco_reentry.phase) {
        const EmPlayerReentry *reentry = &g.loco_reentry;
        unsigned count = g.model.bone_count * 16;
        if (reentry->blend_left == 4) {
            memcpy(g.loco_stop_from, g.player_palette, count * sizeof(float));
            /* Keep the frozen pose in actor space while the player moves.
             * Blending two world placements would leave the displayed body
             * behind its collision position during the request frame. */
            for (unsigned bone=0;bone<g.model.bone_count;++bone) {
                float *matrix=g.loco_stop_from+bone*16;
                for (unsigned axis=0;axis<3;++axis)
                    matrix[12+axis]-=previous_position[axis];
            }
            const float origin[3]={0,0,0};
            palette_apply_placement(g.loco_stop_from,g.model.bone_count,
                                    origin,-previous_yaw);
        }
        em_model_palette_at(&g.model, (uint32_t)g.loco_clip,
                            reentry->frame, g.player_palette);
        float weight = 1.0f - (float)reentry->blend_left / 4.0f;
        for (unsigned i = 0; i < count; ++i)
            g.player_palette[i] = g.loco_stop_from[i] +
                (g.player_palette[i] - g.loco_stop_from[i]) * weight;
        palette_apply_placement(g.player_palette, g.model.bone_count, g.pos, g.yaw);
        return;
    }

    /* The run-stop and return-to-idle callbacks own the pose until their
     * non-looping clip/blend finishes. Matrix interpolation remains the
     * host approximation; clip selection, source time and gates are original. */
    if (g.loco_stop.phase) {
        const EmPlayerStop *stop = &g.loco_stop;
        unsigned count = g.model.bone_count * 16;
        int to_idle = stop->phase == 4;
        unsigned blend_duration = to_idle ? 12 : 6;
        if ((stop->phase == 1 || to_idle) && stop->blend_left == blend_duration)
            memcpy(g.loco_stop_from, g.player_palette, count * sizeof(float));
        int clip = to_idle ? g.clip_idle : g.loco_stop_clip;
        em_model_palette_at(&g.model, (uint32_t)clip,
                            to_idle ? 0 : stop->frame, g.player_palette);
        palette_apply_placement(g.player_palette, g.model.bone_count, g.pos, g.yaw);
        if (stop->phase == 1 || to_idle) {
            float weight = 1.0f - (float)stop->blend_left / blend_duration;
            for (unsigned i = 0; i < count; ++i)
                g.player_palette[i] = g.loco_stop_from[i] +
                    (g.player_palette[i] - g.loco_stop_from[i]) * weight;
        }
        return;
    }

    /* Original 0017B660 selects gait clips at scalar tier boundaries.
     * Pose interpolation below still approximates original bone NLERP
     * with exported matrices; timing/phase do not establish pose fidelity. */
    /* The scalar rate is consumed before the state callback (0015BA50). */
    g.walk_t += (double)g.loco_animation_step / 60.0;
    int loco = loco_clip_for_tier(g.loco_tier);
    if (loco != g.loco_clip) {
        /* 0017B660 transfers normalized cycle position between lengths. */
        if (g.loco_clip >= 0 && loco >= 0) {
            const EmModelClip *old = &g.model.clips[g.loco_clip];
            const EmModelClip *next = &g.model.clips[loco];
            double phase = fmod(g.walk_t * old->fps, old->frame_count)
                           / old->frame_count;
            g.walk_t = phase * next->frame_count / next->fps;
        }
        g.loco_clip  = loco;
        g.loco_speed = (loco >= 0 && loco == g.clip_run) ? RUN_CLIP_SPEED
                     : (loco >= 0 && loco == g.clip_jog) ? JOG_CLIP_SPEED
                                                         : WALK_CLIP_SPEED;
        /* Keep step_prev for the existing footstep edge detector. */
    }

    /* 0017B660 mixes only while the scalar substate rises/falls. A
     * boundary callback selects one clip; +208 can retain its previous
     * value and must not be inferred again from the new tier's speed. */
    int next_tier = g.loco_substate == 1 ? g.loco_tier + 1
                  : g.loco_substate == 2 ? g.loco_tier - 1 : -1;
    int next_loco = next_tier >= 1 && next_tier <= 3
                    ? loco_clip_for_tier(next_tier) : -1;
    float loco_blend = g.loco_mode == 1 && g.loco_tier > 0 &&
                       next_loco >= 0 && next_loco != g.loco_clip
                       ? g.loco_blend : 0;

    float target = 0.0f;
    if (g.loco_clip >= 0) {
        target = (g.move_speed > 0.0f || g.loco_entry_ticks != 0 ||
                  g.loco_mode != 0) ? 1.0f : 0.0f;
        if (g.loco_entry_ticks > 0) {
            g.walk_w = (8.0f - g.loco_entry_ticks) / 8.0f;
        } else if (g.loco_entry_ticks < 0 ||
                   (g.loco_mode == 1 && g.loco_upt == 0)) {
            g.walk_w = 1;
        } else {
            float step = FRAME_DT / ANIM_BLEND_TIME;
            if (g.walk_w < target - step) g.walk_w += step;
            else if (g.walk_w > target + step) g.walk_w -= step;
            else g.walk_w = target;
        }

        /* FOOTSTEP triggers (footstep_play above): the loco-cycle
         * playhead in clip FRAMES — wrapping exactly like the palette
         * evaluation — against the clip's D_00248C90 trigger frames
         * (walk id 1: 72/21; jog id 2: 26/3; run id 3: 21/2). The
         * playhead only advances while moving (rate-scaled to the
         * ground speed), so standing is silent and slower walks space
         * their steps out, exactly like the engine's clip-time test. */
        int run = g.loco_clip == g.clip_run && g.clip_run >= 0;
        int jog = g.loco_clip == g.clip_jog && g.clip_jog >= 0;
        double fa = run ? RUN_STEP_FRAME_A
                  : jog ? JOG_STEP_FRAME_A : WALK_STEP_FRAME_A;
        double fb = run ? RUN_STEP_FRAME_B
                  : jog ? JOG_STEP_FRAME_B : WALK_STEP_FRAME_B;
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        double cyc = fmod(g.walk_t * (double)cw->fps,
                          (double)cw->frame_count);
        if (step_crossed(g.step_prev, cyc, fa) ||
            step_crossed(g.step_prev, cyc, fb))
            footstep_play(run ? 3 : jog ? 2 : 1);
            /* a1 = the TIER (+0x25C): run +0xA, jog +5, walk +0
             * (func_00182430 sub-bases — the s30 "walk 0x15/run 0x1A"
             * captures were tiers 2/3 under the corrected labels) */
        g.step_prev = cyc;
    }

    /* IDLE CYCLE (func_00161020 [NEARMISS — logic authoritative], the
     * case-1 sub-0 branch): while truly idle the 300-frame timer runs
     * (+0x28 = 0x12C is literal); at zero the fidget 349 plays once
     * (func_001749A0(self, 0x15D, 1, 8.0f) — 0x15D = 349, the 8.0 is the
     * blend arg, hence the 8-frame cross-fade each way) and the
     * breathing idle restarts with the timer re-armed (the sub-1 arm's
     * clip-end bit 0x1000 -> +0x28 = 0x12C, func_00174A50(self, 8.0f)).
     * Any movement, gait input, aim, melee or the door input lock leaves
     * mode 0 and resets the cycle.
     *
     * CORRECTED (audit 2026-07-31) — the port used to fidget at ANY
     * health. The engine gates the whole countdown on
     * `+0x236 == 0 && !(+0x235 & 1)`, and +0x235 bit 0 is the LOW-HEALTH
     * latch: SET at health <= 35 (src/func_0021C440.c's tail
     * `if (+0x220 <= 35.0f) +0x235 |= 1`, and src/func_0015D100.c's two
     * decay paths, both `<= 35.0f`), CLEARED again once health is back
     * above it (src/func_0015C700.c: `if (hp > 35.0f) +0x235 &= 0x2`).
     * So a hurt player never plays the look-around fidget — he only
     * breathes; the latch being a pure function of health is why the
     * gate below reads g.status.health directly (same PD_LOW_HEALTH =
     * 35.0f constant the damage block derives from the same tail). It
     * gates only the COUNTDOWN, exactly like the engine: a fidget
     * already in flight when the latch trips plays out and re-arms
     * normally, because the sub-1 arm carries no such gate. (+0x236 is
     * a separate scripted-suppression byte with no native counterpart —
     * deliberately not modelled.) */
    {
        int active = g.move_speed > 0.0f || g.gait != 0 ||
                     em_weapon_is_aiming() || em_weapon_is_melee() ||
                     em_door_movement_locked();
        float fstep = FRAME_DT / IDLE_BLEND_TIME;
        if (active || g.clip_fidget < 0) {
            g.idle_timer = IDLE_FIDGET_FRAMES;
            g.idle_phase = 0;
            g.fid_w     -= fstep;          /* fade an aborted fidget out */
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
        } else if (g.idle_phase == 0) {
            g.fid_w -= fstep;
            if (g.fid_w < 0.0f) g.fid_w = 0.0f;
            /* the LOW-HEALTH latch (+0x235 & 1) FREEZES the countdown —
             * the decrement lives inside the engine's gate, so a hurt
             * player's timer neither runs nor resets */
            if (g.status.health > PD_LOW_HEALTH &&
                --g.idle_timer <= 0) {     /* +0x28 hit 0: fidget */
                g.idle_phase = 1;
                g.fid_t      = 0.0;
            }
        } else {
            g.fid_w += fstep;
            if (g.fid_w > 1.0f) g.fid_w = 1.0f;
            g.fid_t += FRAME_DT;
            const EmModelClip *cf = &g.model.clips[g.clip_fidget];
            if (g.fid_t * cf->fps >= (double)(cf->frame_count - 1)) {
                /* clip-end flag (+0x200 & 0x1000): back to breathing,
                 * re-init (clip restarts at 0), timer re-armed */
                g.idle_phase = 0;
                g.idle_timer = IDLE_FIDGET_FRAMES;
                g.idle_t     = -FRAME_DT;  /* restarts at 0 after the
                                            * post-eval advance below */
            }
        }
    }

    /* DEBUG (EM_CLIP_DEMO=1): turntable the player model through the 4
     * formerly-unbakeable directory clips 54/94/115/375 (s76 baker fix)
     * so they can be eyeballed — each plays ~4 s (looping) while the
     * model slowly spins; the active id + length is printed on each
     * switch. Needs a player.emdl exported WITH those 4 clips. */
    {
        static const int kCD[4] = { 54, 94, 115, 375 };
        static int    cd_on = -1, cd_idx = 0, cd_clip = -2, cd_hold = 0;
        static double cd_t = 0.0, cd_spin = 0.0;
        if (cd_on < 0) cd_on = getenv("EM_CLIP_DEMO") != NULL;
        if (cd_on) {
            if (cd_clip == -2 || cd_hold++ >= 240) {
                if (cd_clip != -2) cd_idx = (cd_idx + 1) & 3;
                cd_hold = 0;
                cd_t    = 0.0;
                cd_clip = em_model_clip_index(&g.model,
                                              (uint32_t)kCD[cd_idx]);
                printf("clip demo: directory id %d -> model clip %d "
                       "(%d frames)\n", kCD[cd_idx], cd_clip,
                       cd_clip >= 0
                       ? g.model.clips[cd_clip].frame_count : 0);
                fflush(stdout);
            }
            int show = cd_clip >= 0 ? cd_clip
                     : (g.clip_idle >= 0 ? g.clip_idle : 0);
            const EmModelClip *cc = &g.model.clips[show];
            double per = (double)cc->frame_count;
            em_model_palette_at(&g.model, (uint32_t)show,
                                per > 1.0 ? fmod(cd_t * cc->fps, per) : 0.0,
                                g.player_palette);
            cd_t    += FRAME_DT;
            cd_spin += (double)FRAME_DT * 0.6;   /* slow turntable */
            palette_apply_placement(g.player_palette, g.model.bone_count,
                                    g.pos, g.yaw + (float)cd_spin);
            return;
        }
    }

    const EmModelClip *ci = &g.model.clips[g.clip_idle];
    em_model_palette_at(&g.model, (uint32_t)g.clip_idle,
                        g.idle_t * ci->fps, g.player_palette);
    if (g.fid_w > 0.0f && g.clip_fidget >= 0) {
        const EmModelClip *cf = &g.model.clips[g.clip_fidget];
        double ft  = g.fid_t * cf->fps;
        double end = (double)(cf->frame_count - 1);
        em_model_palette_at(&g.model, (uint32_t)g.clip_fidget,
                            ft < end ? ft : end, g.walk_palette);
        uint32_t n = g.model.bone_count * 16;
        float    w = g.fid_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    if (g.walk_w > 0.0f && g.loco_clip >= 0) {
        uint32_t n = g.model.bone_count * 16;

        /* current-tier clip into walk_palette (the loco pose base) */
        const EmModelClip *cw = &g.model.clips[g.loco_clip];
        em_model_palette_at(&g.model, (uint32_t)g.loco_clip,
                            g.walk_t * cw->fps, g.walk_palette);

        /* C1: cross-blend the NEXT-tier clip in by the ramp progress
         * (+0x208). Both clips ride the SAME walk_t cycle clock (each
         * scaled by its own fps) so the jog->run morph stays in phase as
         * the cycle continues across the promotion. At progress 0 (a
         * sustained tier) this is a no-op; sweeping 0->1 over the accel
         * frames is the visible build-up. */
        if (next_loco >= 0 && loco_blend > 0.0f) {
            const EmModelClip *cn = &g.model.clips[next_loco];
            em_model_palette_at(&g.model, (uint32_t)next_loco,
                                fmod(g.walk_t * cw->fps, cw->frame_count)
                                * cn->frame_count / cw->frame_count,
                                g.loco_palette);
            float b = loco_blend;
            for (uint32_t i = 0; i < n; i++)
                g.walk_palette[i] += (g.loco_palette[i] -
                                      g.walk_palette[i]) * b;
        }

        /* blend the (morphed) loco pose over idle by walk_w */
        float w = g.walk_w;
        for (uint32_t i = 0; i < n; i++)
            g.player_palette[i] += (g.walk_palette[i] -
                                    g.player_palette[i]) * w;
    }
    palette_apply_placement(g.player_palette, g.model.bone_count,
                            g.pos, g.yaw);
    g.idle_t += FRAME_DT;   /* post-eval, matching the old g.t cadence
                             * (frame n evaluates the idle at n/60) */
}

/* ------------------------------------------------------------------ */
/* Stage functions (S5, docs/SCENE_COORDINATOR_DESIGN.md section 4.5)  */
/* ------------------------------------------------------------------ */

/* The port's damage producers (em_enemy.h: the worm's lunge and the breather
 * pad; no AREA11 owner posts one) leave one mailbox int; the original
 * producers write the pending-damage floats directly during the pool walk:
 * +224 (health; the lunge's 15, D_008104D4) or +22C (infection; the open
 * pad's 5.0, D_008104DC). This maps the mailbox onto the port's storage of
 * those two floats before the player stage reads them (0021C440 on the
 * stage, census L01), so a hit posted during frame N's pool walk is taken by
 * frame N+1's stage, as in the original. */
static void player_hit_mailbox(void)
{
    int hitcode = em_enemy_player_hit_take();
    if (hitcode & 0x4000)
        g.pd_pend_hp += (float)(hitcode & 0xFFF);
    else if (hitcode)
        g.pd_pend_inf += (float)hitcode;
}

/* func_0015BCF0 position of func_001AE5E0 (design section 2.4, item 3),
 * reached through the bindings' w_0015BCF0 with a0 = the player 0x8102B0.
 * The surrounding 001CB590(player) and 001CB5A0 (an empty leaf) are the
 * bindings' own workers since S10a. player_pose_finish_palette stays here
 * because that is where it ran (design 10.2 Q2: the original produces the
 * final palette inside 0015BCF0). Since S11b this stage also runs:
 *   - since census L01, the original stage itself (actor_update ->
 *     player_states_stage): 0021C440, 0015D100, 0015D000 and 0015BCF0's
 *     -200 check replaced em_player_damage.c's copies and kill plane; the
 *     port's hit mailbox is mapped before it (player_hit_mailbox) and the
 *     legacy bug-latch struggle ticks after it;
 *   - the menu-inhibit byte B3 (D_008106B3), whose original writer is
 *     0015BA50's tail (em_hud.h "STATUS OPEN/CLOSE" lists its conditions).
 *     Since L01 the translated tail computes them into the stage's view
 *     (player_states_busy), but the canonical byte still takes the legacy
 *     screen's former open gate (damage/death lock, examine sequence,
 *     opening runtime, door menu lock): switching it to the translated
 *     result changes which frames the status screen may open during the
 *     legacy door / examine / opening stand-ins, which no capture
 *     comparison covers yet (FIRST_CONTROL.md "D_008106B3");
 *   - 0015CF90 (byte-matched, src/func_0015CF90.c): D_00810707 =
 *     +0x234 into the canonical progress byte (HK; +0x234 is the
 *     port's g.pd_infected, which the stage's vitals view stores back
 *     after every stage since L01), then the B9 write
 *     `if (+0x220 <= 0.0f && D_008106B9 == 0) D_008106B9 = 1`, +0x220
 *     being the player's health (g.status.health). 0015CF90's other
 *     stores (D_00810706 = +0x235, D_00810858/85C = +0x220/+0x228)
 *     target progress bytes that are not canonical yet (D2); 001B07C0
 *     reads the port's g.pd_low/g.status for them.
 * In the cutscene variant the bindings do not call this: the port poses
 * the player through the opening runtime (design risk 2). */
int em_player_0015BCF0(void)
{
    player_hit_mailbox();
    actor_update();          /* func_0015BCF0 — player actor update   */
    player_pose_finish_palette(); /* original hip/Euler scratch publication */
    player_struggle_tick();  /* legacy bug-latch struggle (mash CROSS) */
    EmSceneState *scene = em_scene_state();
    scene->req[EM_SCENE_REQ_B3] = (uint8_t)(player_damage_locked() ||
                                            em_examine_input_locked() ||
                                            em_opening_runtime_busy() ||
                                            em_door_menu_locked());
    uint8_t *d810707 = em_scene_progress_at(scene, 0x00810707u, 1);
    if (!d810707)
        return -1;
    *d810707 = (uint8_t)g.pd_infected;                 /* 0015CF90: D_00810707 = +0x234 */
    if (g.status.health <= 0.0f && scene->req[EM_SCENE_REQ_B9] == 0)
        scene->req[EM_SCENE_REQ_B9] = 1;
    return 0;
}
