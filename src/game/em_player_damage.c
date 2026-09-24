/* em_player_damage.c — the legacy bug-latch struggle and its death.
 *
 * What is left of the port's damage pipeline: the lock gate, the struggle
 * (a stand-in for the +4 = 2 +5 = 0xD latch reaction 002208C0) with its
 * death entry and the hurt tick that plays them. Census L01 retired the
 * port's copies of 0021C440 (the damage processor with its 0021C350 /
 * 0021C270 appliers and the flinch entry), 0015D100 (the infected drain),
 * the +20E countdown and the kill plane: the original player stage runs the
 * translations (em_player_stage_workers.c, em_player_floor.c's 0015B130 and
 * em_player_stage_tail's -200 check) over the player record
 * (em_player.c player_states_stage).
 *
 * Every function here reads the shared gameplay state (EmGameState g), so
 * this module takes the subsystem's internal header rather than owning
 * private state — the same single state block the engine keeps in its
 * gameplay globals, now viewed from one more file. */

#include "game/em_player_damage.h"

#include "game/em_game_internal.h"

/* Is the player in a hit reaction / dying / at the game-over screen?
 * (the movement + input + menu lock; also the producer-side immunity
 * gate together with pd_iframes — engine: every producer requires the
 * event byte == 1, which holds from reaction start until the +0x20E
 * window expires after the flinch). */
int player_damage_locked(void)
{
    return g.pd_state != 0 || g.go_state != 0;
}

/* state 2 sub 1/3 phase 0 — DEATH entry (func_0021E240 /
 * func_0021E830). The +0x1F0 = 0x40/0x3F write is only the category
 * marker — the sub handler requests the REAL clip itself. */
static void player_enter_death(void)
{
    int      armed = em_weapon_state() != EM_WPN_HOLSTERED;  /* +0x236 */
    unsigned clip  = g.pd_infected ? PD_CLIP_DEATH_INF
                   : armed         ? PD_CLIP_DEATH_ARM
                                   : PD_CLIP_DEATH;
    em_sfx_play_at(PD_SFX_DEATH_VOICE, g.pos, 300.0f); /* 0x146, r=300 */
    em_sfx_play_at(PD_SFX_DEATH_BODY,  g.pos, 300.0f); /* 0x151, r=300 */
    if (!em_game_anim_hold(clip, 1.0f))
        clip = 0;                  /* clip-less asset: straight to hold */
    g.pd_state    = 2;
    g.pd_sub      = g.pd_infected ? 3 : 1;
    g.pd_phase    = clip ? 5 : 2;  /* 5 = wait commit */
    g.pd_hold     = PD_CORPSE_HOLD;
    g.pd_clip     = clip;
    g.pd_cue_fall = 0;
    g.pd_cue_thud = 0;
    /* gore effect 0x80000051 (infected) — no effect system: flagged */
    printf("player death: clip 0x%X (%s)%s\n", clip,
           g.pd_infected ? "infected" : armed ? "armed" : "unarmed",
           clip ? "" : " — EMDL carries no death clip (timed fallback)");
}

/* s76 BUG SHAKE-OFF (the latch reaction — func_002208C0 / state-2
 * sub-state 0xD): a bug bite LATCHES the bug onto the player (em_enemy
 * sub 5); while any cling, the player DRAINS, and when free he SHAKES
 * them off (clip 54), which detaches them all. The engine's 0x35->0x36
 * ->0x35 wrap is reduced to the recognisable 0x36 shake. */
static void player_enter_struggle(void)
{
    em_game_anim_hold(PD_CLIP_CLING, 1.0f);   /* 0x2C cling-idle pose */
    em_sfx_play_at(PD_SFX_SHAKE, g.pos, 300.0f);
    g.pd_state    = 2;
    g.pd_sub      = 5;               /* STRUGGLE (mash CROSS)            */
    g.pd_phase    = 1;              /* skip the wait-commit gate         */
    g.pd_hold     = 0;
    g.pd_clip     = PD_CLIP_CLING;
    g.struggle_n  = 0;
    g.status.health -= PD_LATCH_HIT;          /* 10 HP on connect (live) */
    if (g.status.health < 0.0f) g.status.health = 0.0f;
}

/* LIVE-VERIFIED 2026-06-12 (PCSX2): a clinging bug is shaken off by
 * MASHING CROSS, not automatically; while it clings the player loses
 * HEALTH and (the real killer) gains INFECTION fast; winning the mash
 * race throws the bug off and KILLS it, losing it -> infected death.
 * Rates/threshold are flagged PORT constants (need a live read —
 * FINDINGS "BUG LATCH / SHAKE-OFF — LIVE-VERIFIED"). */
void player_struggle_tick(void)
{
    int latched = em_enemy_latched_count();
    if (latched <= 0)
        return;
    /* DRAIN + INFECT while any bug clings (direct — the reaction locks
     * the normal damage pipeline). Infection maxing or health 0 = death. */
    if (g.go_state == 0 && g.pd_sub != 1 && g.pd_sub != 3) {
        g.status.infection += PD_LATCH_INFECT * (float)latched;
        if (g.status.infection > 100.0f) g.status.infection = 100.0f;
        g.status.health    -= PD_LATCH_DRAIN  * (float)latched;
        if (g.status.infection >= 100.0f && !g.pd_infected) {
            g.pd_infected       = 1;            /* infected latch (cap 60) */
            g.status.health_max = PD_INFECTED_MAX;
            em_sfx_play_at(PD_SFX_INFECTED, g.pos, 300.0f);
        }
        if (g.status.health <= 0.0f) {
            g.status.health = 0.0f;
            g.pd_state = 0;
            g.pd_phase = 0;
            em_enemy_shake_off();              /* the bugs go with you   */
            player_enter_death();
            return;
        }
    }
    /* enter the struggle the moment a bug first clings (engine: the
     * contact raises +0x0F=2 when the player is free / not i-framed). */
    if (g.pd_state == 0 && g.go_state == 0 && g.pd_iframes == 0)
        player_enter_struggle();
}

/* The struggle's per-frame tick (the port slice of the latch reaction and
 * of func_0021E240 / func_0021D2E0 for its death): the mash, the throw-off,
 * then for a death the committed clip's sound cues, the corpse hold and the
 * fade-out into the game-over stand-in. Runs INSTEAD of the player stage
 * (em_player_frame.c actor_update). */
void player_hurt_tick(void)
{
    if (g.pd_phase == 5) {
        /* wait for the scripted-anim COMMIT (1-frame latency — the
         * engine requests land one update after the write too) */
        if (em_game_anim_active() == g.pd_clip)
            g.pd_phase = 1;
        return;
    }
    if (g.pd_sub == 5) {
        /* STRUGGLE: mash CROSS to advance the counter (live: each press
         * advances it; no decay). At the threshold the bugs are thrown
         * off and DIE (em_enemy_shake_off). The cling pose (0x2C) holds;
         * each press plays the struggle flourish (0x2E). */
        const EmFrameInput *in = em_frame_input();
        if (in && (in->pressed & EM_PAD_CROSS)) {
            g.struggle_n++;
            em_game_anim_request(PD_CLIP_STRUGGLE, 1.2f);   /* 0x2E */
        } else if (em_game_anim_active() == 0) {
            em_game_anim_hold(PD_CLIP_CLING, 1.0f);          /* re-hold 0x2C */
        }
        if (g.struggle_n >= PD_STRUGGLE_WIN) {
            em_enemy_shake_off();              /* thrown off -> the bugs DIE */
            em_sfx_play_at(PD_SFX_THROWOFF, g.pos, 300.0f);  /* 0x14D */
            em_game_anim_request(PD_CLIP_THROWOFF, 1.0f);    /* 0x24 */
            g.pd_sub   = 6;
            g.pd_phase = 5;
            g.pd_clip  = PD_CLIP_THROWOFF;
            g.pd_hold  = 0;
        } else if (em_enemy_latched_count() == 0) {
            g.pd_state   = 0;                  /* bugs gone (shot off) -> exit */
            g.pd_phase   = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    if (g.pd_sub == 6) {
        /* THROW-OFF recover: the 0x24 clip plays out, then exit + invuln. */
        int over = g.pd_phase == 2
                 ? ++g.pd_hold >= 30
                 : em_game_anim_active() != g.pd_clip;
        if (over) {
            g.pd_state   = 0;
            g.pd_phase   = 0;
            g.pd_hold    = 0;
            g.pd_iframes = PD_IFRAMES;
        }
        return;
    }
    /* DEATH (sub 1 normal / 3 infected) */
    if (g.pd_phase == 1) {
        /* clip playing: frames-remaining sound cues (func_0021E240
         * phase 1 reads the anim clock +0x3C the same way) */
        int total = em_game_anim_frames(g.pd_clip);
        int cur   = em_game_anim_frame();
        int rem   = (cur >= 0 && total > 0) ? total - 1 - cur : -1;
        if (!g.pd_cue_fall && rem >= 0 && rem <= PD_DEATH_CUE_FALL) {
            g.pd_cue_fall = 1;
            em_sfx_play_at(PD_SFX_DEATH_FALL, g.pos, 300.0f); /* 0x156 */
        }
        if (!g.pd_cue_thud && rem >= 0 && rem <= PD_DEATH_CUE_THUD) {
            g.pd_cue_thud = 1;
            em_sfx_play_at(g.pd_infected ? PD_SFX_DEATH_THUD_I
                                         : PD_SFX_DEATH_THUD,
                           g.pos, 300.0f);
            /* + the heavy ground rumble func_001B61C0(1,0xEE,0x3C,1) */
        }
        if (rem <= 0) {
            /* clip done (held on the last frame — the corpse pose):
             * func_0021D2E0 phase 0. Blood-pool effect 0x80000043
             * under the corpse: skipped (no decal system), flagged. */
            g.pd_phase = 2;
        }
        return;
    }
    if (g.pd_phase == 2) {
        /* corpse hold (+0x28 = 0x78), then the standard fade-out —
         * func_001AEDE0(4,0), the door-transit machine. */
        if (--g.pd_hold <= 0) {
            g.pd_phase = 3;
            em_frame_fade_start(1, EM_FADE_SPEED_DOOR);
            g.go_state = 1;
        }
        return;
    }
    /* phase 3: parked dead in the fade-out. The hand-off at full black
     * is the frame machine's since S11b: B9 was written by the player
     * stage (0015CF90) when health reached 0, and 0x1AE040 state 1 calls
     * 001AD140 once D_0028A9A0 == 2 (after the variant), which moves the
     * game task to the byte-matched 001AD4E0 game-over screen; the world
     * frame (this tick) no longer runs after that. */
}
